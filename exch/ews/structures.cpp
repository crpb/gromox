// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2022-2023 grommunio GmbH
// This file is part of Gromox.
/**
 * @brief      Implementation of EWS structure methods
 *
 * This file only contains data type logic, the implementation
 * of (de-)serialization functions was moved to serialization.cpp.
 */
#include <algorithm>
#include <iterator>

#include <gromox/ext_buffer.hpp>
#include <gromox/fileio.h>
#include <gromox/mapi_types.hpp>
#include <gromox/rop_util.hpp>
#include <gromox/ical.hpp>

#include "ews.hpp"
#include "structures.hpp"

using namespace gromox::EWS;
using namespace gromox::EWS::Exceptions;
using namespace gromox::EWS::Structures;
using namespace std::string_literals;
using namespace tinyxml2;

namespace
{

inline gromox::time_point nttime_to_time_point(uint64_t nttime)
{return gromox::time_point::clock::from_time_t(rop_util_nttime_to_unix(nttime));}

}

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief     Initilize binary data from tagged propval
 *
 * Propval type must be PT_BINARY.
 */
sBase64Binary::sBase64Binary(const TAGGED_PROPVAL& tp)
{
	if(PROP_TYPE(tp.proptag) != PT_BINARY)
		throw DispatchError("Can only convert binary properties to Base64Binary");
	const BINARY* bin = static_cast<const BINARY*>(tp.pvalue);
	assign(bin->pb, bin->pb+bin->cb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#define TRY(expr) EWSContext::ext_error(expr)

/**
 * @brief     Parse entry ID from binary data
 *
 * @param     data     Buffer containing the entry ID
 * @param     size     Size of the buffer
 */
sFolderEntryId::sFolderEntryId(const void* data, uint64_t size)
{init(data, size);}

/**
 * @brief     Parse entry ID from binary data
 *
 * @param     data     Buffer containing the entry ID
 * @param     size     Size of the buffer
 */
void sFolderEntryId::init(const void* data, uint64_t size)
{
	EXT_PULL ext_pull;
	if(size >	std::numeric_limits<uint32_t>::max())
		throw DeserializationError("Folder entry ID data to large");
	ext_pull.init(data, uint32_t(size), EWSContext::alloc, 0);
	TRY(ext_pull.g_folder_eid(this));
}

/**
 * @brief     Retrieve account ID from entry ID
 *
 * @return    User or domain ID (depending on isPrivate())
 */
uint32_t sFolderEntryId::accountId() const
{return database_guid.time_low;}

/**
 * @brief     Retrieve folder ID from entryID
 *
 * @return    Folder ID
 */
uint64_t sFolderEntryId::folderId() const
{return rop_util_gc_to_value(global_counter);}

/**
 * @brief     Retrieve folder type
 *
 * @return    true if folder is private, false otherwise
 */
bool sFolderEntryId::isPrivate() const
{return folder_type == EITLT_PRIVATE_FOLDER;}

#undef TRY

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * List of known distinguished folder IDs
 *
 * Must be sorted alphabetically by name.
 */
decltype(sFolderSpec::distNameInfo) sFolderSpec::distNameInfo = {{
    {"calendar", PRIVATE_FID_CALENDAR, true},
    {"conflicts", PRIVATE_FID_CONFLICTS, true},
    {"contacts", PRIVATE_FID_CONTACTS, true},
    {"deleteditems", PRIVATE_FID_DELETED_ITEMS, true},
    {"drafts", PRIVATE_FID_DRAFT, true},
    {"imcontactlist", PRIVATE_FID_IMCONTACTLIST, true},
    {"inbox", PRIVATE_FID_INBOX, true},
    {"journal", PRIVATE_FID_JOURNAL, true},
    {"junkemail", PRIVATE_FID_JUNK, true},
    {"localfailures", PRIVATE_FID_LOCAL_FAILURES, true},
    {"msgfolderroot", PRIVATE_FID_IPMSUBTREE, true},
    {"notes", PRIVATE_FID_NOTES, true},
    {"outbox", PRIVATE_FID_OUTBOX, true},
    {"publicfoldersroot", PUBLIC_FID_IPMSUBTREE, false},
    {"quickcontacts", PRIVATE_FID_QUICKCONTACTS, true},
    {"root", PRIVATE_FID_ROOT, true},
    {"scheduled", PRIVATE_FID_SCHEDULE, true},
    {"sentitems", PRIVATE_FID_SENT_ITEMS, true},
    {"serverfailures", PRIVATE_FID_SERVER_FAILURES, true},
    {"syncissues", PRIVATE_FID_SYNC_ISSUES, true},
    {"tasks", PRIVATE_FID_TASKS, true},
}};

/**
 * @brief     Derive folder specification from distinguished ID
 *
 * @param     folder  Distinguished ID
 */
sFolderSpec::sFolderSpec(const tDistinguishedFolderId& folder)
{
	auto it = std::find_if(distNameInfo.begin(), distNameInfo.end(),
	                       [&folder](const auto& elem){return folder.Id == elem.name;});
	if(it == distNameInfo.end())
		throw DeserializationError("Unknown distinguished folder id "+folder.Id);
	folderId = rop_util_make_eid_ex(1, it->id);
	location = it->isPrivate? PRIVATE : PUBLIC;
	if(folder.Mailbox)
		target = folder.Mailbox->EmailAddress;
}

/**
 * @brief     Explicit initialization for direct serialization
 */
sFolderSpec::sFolderSpec(const std::string& target, uint64_t folderId) :
    target(target), folderId(folderId)
{}

/**
 * @brief     Trim target specification according to location
 */
sFolderSpec& sFolderSpec::normalize()
{
	if(location != PUBLIC || !target)
		return *this;
	size_t at = target->find('@');
	if(at == std::string::npos)
		return *this;
	target->erase(0, at+1);
	return *this;
}

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief     Default constructor
 *
 * Initializes given and seen member for deserialization
 */
sSyncState::sSyncState() :
    given(false, REPL_TYPE_ID),
    seen(false, REPL_TYPE_ID),
    read(false, REPL_TYPE_ID),
    seen_fai(false, REPL_TYPE_ID)
{}

/**
 * @brief     Deserialize sync state
 *
 * @param     data64  Base64 encoded data
 */
void sSyncState::init(const std::string& data64)
{
	EXT_PULL ext_pull;
	TPROPVAL_ARRAY propvals;

	std::string data = base64_decode(data64);

	seen.clear();
	given.clear();
	read.clear();
	seen_fai.clear();
	if(data.size() <= 16)
		return;
	if(data.size() > std::numeric_limits<uint32_t>::max())
		throw InputError("Sync state too big");
	ext_pull.init(data.data(), uint32_t(data.size()), EWSContext::alloc, 0);
	if(ext_pull.g_tpropval_a(&propvals) != EXT_ERR_SUCCESS)
		return;
	for (TAGGED_PROPVAL* propval = propvals.ppropval; propval < propvals.ppropval+propvals.count; ++propval)
	{
		switch (propval->proptag) {
		case MetaTagIdsetGiven1:
			if(!given.deserialize(static_cast<BINARY *>(propval->pvalue)) || !given.convert())
				throw InputError("Failed to deserialize given idset");
			break;
		case MetaTagCnsetSeen:
			if(!seen.deserialize(static_cast<BINARY *>(propval->pvalue)) || !seen.convert())
				throw InputError("Failed to deserialize seen cnset");
			break;
		case MetaTagCnsetRead:
			if(!read.deserialize(static_cast<BINARY *>(propval->pvalue)) || !read.convert())
				throw InputError("Failed to deserialize read cnset");
			break;
		case MetaTagCnsetSeenFAI:
			if(!seen_fai.deserialize(static_cast<BINARY *>(propval->pvalue)) || !seen_fai.convert())
				throw InputError("Failed to deserialize seen fai cnset");
			break;
		}
	}
}

/**
 * @brief     Update sync state with given and seen information
 *
 * @param     given_fids  Ids marked as given
 * @param     lastCn      Change number marked as seen
 */
void sSyncState::update(const EID_ARRAY& given_fids, const EID_ARRAY& deleted_fids, uint64_t lastCn)
{
	seen.clear();
	given.convert();
	for(uint64_t* pid = deleted_fids.pids; pid < deleted_fids.pids+deleted_fids.count; ++pid)
		given.remove(*pid);
	for(uint64_t* pid = given_fids.pids; pid < given_fids.pids+given_fids.count; ++pid)
		if(!given.append(*pid))
			throw DispatchError("Failed to generated sync state idset");
	seen.convert();
	if(lastCn && !seen.append_range(1, 1, rop_util_get_gc_value(lastCn)))
		throw DispatchError("Failed to generate sync state cnset");
}

///////////////////////////////////////////////////////////////////////////////

sTimePoint::sTimePoint(const gromox::time_point& tp) : time(tp)
{}

sTimePoint::sTimePoint(const gromox::time_point& tp, const tSerializableTimeZone& tz) :
    time(tp), offset(tz.offset(tp))
{}

/**
 * @brief      Generate time point from NT timestamp
 */
sTimePoint sTimePoint::fromNT(uint64_t timestamp)
{return gromox::time_point::clock::from_time_t(rop_util_nttime_to_unix(timestamp));}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Types implementation

/**
 * @brief     Convert propvals to structured folder information
 *
 * @param     folderProps     folder property values
 */
tBaseFolderType::tBaseFolderType(const TPROPVAL_ARRAY& folderProps)
{
	tFolderId& fId = FolderId.emplace();
	for(const TAGGED_PROPVAL* tp = folderProps.ppropval; tp < folderProps.ppropval+folderProps.count; ++tp)
	{
		switch(tp->proptag)
		{
		case PR_CONTENT_UNREAD:
			break;
		case PR_CHANGE_KEY:
			fId.ChangeKey.emplace(*tp); break;
		case PR_CONTAINER_CLASS:
			FolderClass = reinterpret_cast<const char*>(tp->pvalue); break;
		case PR_CONTENT_COUNT:
			TotalCount = *reinterpret_cast<uint32_t*>(tp->pvalue); break;
		case PR_DISPLAY_NAME:
			DisplayName = reinterpret_cast<const char*>(tp->pvalue); break;
		case PR_ENTRYID:
			fId.Id = *tp; break;
		case PR_FOLDER_CHILD_COUNT:
			ChildFolderCount = *reinterpret_cast<uint32_t*>(tp->pvalue); break;
		case PR_PARENT_ENTRYID:
			ParentFolderId.emplace().Id = *tp; break;
		default:
			ExtendedProperty.emplace_back(*tp);
		}
	}
}

/**
 * @brief     Create folder from properties
 *
 * Automatically uses information from the tags to fill in folder id and type.
 *
 * @param     folderProps     Folder properties
 *
 * @return    Variant containing the folder information struct
 */
sFolder tBaseFolderType::create(const TPROPVAL_ARRAY& folderProps)
{
	enum Type : uint8_t {NORMAL, CALENDAR, TASKS, CONTACTS, SEARCH};
	const char* frClass = folderProps.get<const char>(PR_CONTAINER_CLASS);
	Type folderType = NORMAL;
	if(frClass)
	{
		if(!strncmp(frClass, "IPF.Appointment", 15))
			folderType = CALENDAR;
		else if(!strncmp(frClass, "IPF.Contact", 11))
			folderType = CONTACTS;
		else if(!strncmp(frClass, "IPF.Task", 8))
			folderType = TASKS;
	}
	switch(folderType)
	{
	case CALENDAR:
		return tCalendarFolderType(folderProps);
	case CONTACTS:
		return tContactsFolderType(folderProps);
	case SEARCH:
		return tSearchFolderType(folderProps);
	case TASKS:
		return tTasksFolderType(folderProps);
	default:
		return tFolderType(folderProps);
	}
}

tBaseItemId::tBaseItemId(const sBase64Binary& fEntryID, const std::optional<sBase64Binary>& chKey) :
    Id(fEntryID), ChangeKey(chKey)
{}

///////////////////////////////////////////////////////////////////////////////

tDistinguishedFolderId::tDistinguishedFolderId(const std::string_view& name) :
    Id(name)
{}

///////////////////////////////////////////////////////////////////////////////

decltype(tExtendedFieldURI::typeMap) tExtendedFieldURI::typeMap = {{
	{"ApplicationTime", PT_APPTIME},
	{"ApplicationTimeArray", PT_MV_APPTIME},
	{"Binary", PT_BINARY},
	{"BinaryArray", PT_MV_BINARY},
	{"Boolean", PT_BOOLEAN},
	{"CLSID", PT_CLSID},
	{"CLSIDArray", PT_MV_CLSID},
	{"Currency", PT_CURRENCY},
	{"CurrencyArray", PT_MV_CURRENCY},
	{"Double", PT_DOUBLE},
	{"DoubleArray", PT_MV_DOUBLE},
	{"Error", PT_ERROR},
	{"Float", PT_FLOAT},
	{"FloatArray", PT_MV_FLOAT},
	{"Integer", PT_LONG},
	{"IntegerArray", PT_MV_LONG},
	{"Long", PT_I8},
	{"LongArray", PT_MV_I8},
	{"Null", PT_UNSPECIFIED},
	{"Object", PT_OBJECT},
	//{"ObjectArray", ???},
	{"Short", PT_SHORT},
	{"ShortArray", PT_MV_SHORT},
	{"String", PT_UNICODE},
	{"StringArray", PT_MV_UNICODE},
	{"SystemTime", PT_SYSTIME},
	{"SystemTimeArray", PT_MV_SYSTIME},
}};

/**
 * @brief     Generate URI from tag ID
 *
 * @param     tag     Property tag ID
 */
tExtendedFieldURI::tExtendedFieldURI(uint32_t tag) :
    PropertyTag(std::in_place_t(), 6, '0'),
    PropertyType(typeName(PROP_TYPE(tag)))
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string& proptag = *PropertyTag;
	proptag[0] = '0';
	proptag[1] = 'x';
	proptag[2] = digits[(tag >> 28) & 0xF];
	proptag[3] = digits[(tag >> 24) & 0xF];
	proptag[4] = digits[(tag >> 20) & 0xF];
	proptag[5] = digits[(tag >> 16) & 0xF];
}

/**
 * @brief     Collect property tags and names for field URI

 * @param     tags    Inserter to use for property tags
 * @param     names   Inserter to use for property names
 * @param     types   Inserter to use for the type of each named property
 */
void tExtendedFieldURI::tags(vector_inserter<uint32_t>& tags, vector_inserter<PROPERTY_NAME>& names,
                             vector_inserter<uint16_t>& types) const
{
	static auto compval = [](const TMEntry& v1, const char* const v2){return strcmp(v1.first, v2) < 0;};
	auto type = std::lower_bound(typeMap.begin(), typeMap.end(), PropertyType.c_str(), compval);
	if(type == typeMap.end() || strcmp(type->first, PropertyType.c_str()))
		throw InputError("Unknown tag type "+PropertyType);
	if(PropertyTag)
	{
		unsigned long long tagId = std::stoull(*PropertyTag, nullptr, 16);
		tags = PROP_TAG(type->second, tagId);
	}
	else if(PropertySetId)
	{
		PROPERTY_NAME name;
		name.guid = *PropertySetId;
		if(PropertyName)
		{
			name.kind = MNID_STRING;
			name.pname = const_cast<char*>(PropertyName->c_str());
		}
		else if(PropertyId)
		{
			name.kind = MNID_ID;
			name.lid = *PropertyId;
		}
		else
			throw InputError("Invalid ExtendedFieldURI: missing name or ID");
		names = name;
		types = type->second;
	}
	else
		throw InputError("Invalid ExtendedFieldURI: missing tag or set ID");
}

/**
 * @brief     Get EWS type name from tag type
 *
 * @param     type    Tag type to convert
 *
 * @return    EWS type name
 */
const char* tExtendedFieldURI::typeName(uint16_t type)
{
	switch(type)
	{
	case PT_MV_APPTIME: return "ApplicationTimeArray";
	case PT_APPTIME: return "ApplicationTime";
	case PT_BINARY: return "Binary";
	case PT_MV_BINARY: return "BinaryArray";
	case PT_BOOLEAN: return "Boolean";
	case PT_CLSID: return "CLSID";
	case PT_MV_CLSID: return "CLSIDArray";
	case PT_CURRENCY: return "Currency";
	case PT_MV_CURRENCY: return "CurrencyArray";
	case PT_DOUBLE: return "Double";
	case PT_MV_DOUBLE: return "DoubleArray";
	case PT_ERROR: return "Error";
	case PT_FLOAT: return "Float";
	case PT_MV_FLOAT: return "FloatArray";
	case PT_LONG: return "Integer";
	case PT_MV_LONG: return "IntegerArray";
	case PT_I8: return "Long";
	case PT_MV_I8: return "LongArray";
	case PT_UNSPECIFIED: return "Null";
	case PT_OBJECT: return "Object";
	case PT_SHORT: return "Short";
	case PT_MV_SHORT: return "ShortArray";
	case PT_UNICODE: return "String";
	case PT_MV_UNICODE: return "StringArray";
	case PT_SYSTIME: return "SystemTime";
	case PT_MV_SYSTIME: return "SystemTimeArray";
	default: return "Unknown";
	}
}

///////////////////////////////////////////////////////////////////////////////

tExtendedProperty::tExtendedProperty(const TAGGED_PROPVAL& tp) : propval(tp)
{}

void tExtendedProperty::serialize(const void* data, size_t idx, uint16_t type, XMLElement* xml) const
{
	switch(type)
	{
	case PT_BOOLEAN:
		return xml->SetText(bool(*(reinterpret_cast<const char*>(data)+idx)));
	case PT_SHORT:
		return xml->SetText(*(reinterpret_cast<const uint16_t*>(data)+idx));
	case PT_LONG:
	case PT_ERROR:
		return xml->SetText(*(reinterpret_cast<const uint32_t*>(data)+idx));
	case PT_I8:
	case PT_CURRENCY:
	case PT_SYSTIME:
		return xml->SetText(*(reinterpret_cast<const uint64_t*>(data)+idx));
	case PT_FLOAT:
		return xml->SetText(*(reinterpret_cast<const float*>(data)+idx));
	case PT_DOUBLE:
	case PT_APPTIME:
		return xml->SetText(*(reinterpret_cast<const double*>(data)+idx));
	case PT_STRING8:
	case PT_UNICODE:
		return xml->SetText((reinterpret_cast<const char*>(data)));
	}
}

///////////////////////////////////////////////////////////////////////////////

decltype(tFieldURI::tagMap) tFieldURI::tagMap = {
	{"folder:FolderId", PidTagFolderId},
	{"folder:ParentFolderId", PR_PARENT_ENTRYID},
	{"folder:DisplayName", PR_DISPLAY_NAME},
	{"folder:UnreadCount", PR_CONTENT_UNREAD},
	{"folder:TotalCount", PR_CONTENT_COUNT},
	{"folder:ChildFolderCount", PR_FOLDER_CHILD_COUNT},
	{"folder:FolderClass", PR_CONTAINER_CLASS},
	{"item:ConversationId", PR_CONVERSATION_ID},
	{"item:DisplayTo", PR_DISPLAY_TO},
    {"item:DateTimeReceived", PR_MESSAGE_DELIVERY_TIME},
    {"item:DateTimeSent", PR_CLIENT_SUBMIT_TIME},
    {"item:HasAttachments", PR_HASATTACH},
	{"item:Importance", PR_IMPORTANCE},
    {"item:InReplyTo", PR_IN_REPLY_TO_ID},
	{"item:IsAssociated", PR_ASSOCIATED},
	{"item:ItemClass", PR_MESSAGE_CLASS},
	{"item:Size", PR_MESSAGE_SIZE_EXTENDED},
	{"item:Subject", PR_SUBJECT},
	{"message:ConversationIndex", PR_CONVERSATION_INDEX},
	{"message:ConversationTopic", PR_CONVERSATION_TOPIC},
	{"message:From", PR_SENT_REPRESENTING_ADDRTYPE},
	{"message:From", PR_SENT_REPRESENTING_EMAIL_ADDRESS},
	{"message:From", PR_SENT_REPRESENTING_NAME},
	{"message:InternetMessageId", PR_INTERNET_MESSAGE_ID},
	{"message:IsRead", PR_READ},
	{"message:References", PR_INTERNET_REFERENCES},
	{"message:Sender", PR_SENDER_ADDRTYPE},
	{"message:Sender", PR_SENDER_EMAIL_ADDRESS},
	{"message:Sender", PR_SENDER_NAME},
};

decltype(tFieldURI::nameMap) tFieldURI::nameMap = {
	{"item:Categories", {{MNID_STRING, PS_PUBLIC_STRINGS, 0, const_cast<char*>("Keywords")}, PT_MV_STRING8}}
};

/**
 * @brief     Collect property tags and names for field URI

 * @param     tags    Inserter to use for property tags
 * @param     names   Inserter to use for property names
 * @param     types   Inserter to use for the type of each named property
 */
void tFieldURI::tags(vector_inserter<uint32_t>& tagins, vector_inserter<PROPERTY_NAME>& nameins,
                     vector_inserter<uint16_t>& typeins) const
{
	auto tags = tagMap.equal_range(FieldURI);
	for(auto it = tags.first; it != tags.second; ++it)
		tagins = it->second;
	auto names = nameMap.equal_range(FieldURI);
	for(auto it = names.first; it != names.second; ++it)
	{
		nameins = it->second.first;
		typeins = it->second.second;
	}
}

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief     Collect property tags and names for folder shape

 * @param     tags    Inserter to use for property tags
 * @param     names   Inserter to use for property names
 * @param     types   Inserter to use for the type of each named property
 */
void tFolderResponseShape::tags(vector_inserter<uint32_t>& tagIns, vector_inserter<PROPERTY_NAME>& nameIns,
                                vector_inserter<uint16_t>& typeIns) const
{
	size_t baseShape = BaseShape.index();
	for(uint32_t tag : tagsIdOnly)
		tagIns = tag;
	if(baseShape >= 1)
		for(uint32_t tag : tagsDefault)
			tagIns = tag;
	if(AdditionalProperties)
		for(const auto& additional : *AdditionalProperties)
			additional.tags(tagIns, nameIns, typeIns);
}

///////////////////////////////////////////////////////////////////////////////

tFolderType::tFolderType(const TPROPVAL_ARRAY& folderProps) :
    tBaseFolderType(folderProps)
{
	if(folderProps.has(PR_CONTENT_UNREAD))
		UnreadCount = *folderProps.get<uint32_t>(PR_CONTENT_UNREAD);
}

///////////////////////////////////////////////////////////////////////////////

tGuid::tGuid(const XMLAttribute* xml)
{from_str(xml->Value());}

std::string tGuid::serialize() const
{
	std::string repr(36, '\0');
	to_str(repr.data(), 37);
	return repr;
}

///////////////////////////////////////////////////////////////////////////////

#define pval(type) static_cast<const type*>(tp->pvalue)

tItem::tItem(const TPROPVAL_ARRAY& propvals, const sNamedPropertyMap&)
{
	for(const TAGGED_PROPVAL* tp = propvals.ppropval; tp < propvals.ppropval+propvals.count; ++tp)
		switch(tp->proptag)
		{
		case PR_CONVERSATION_INDEX:
		case PR_CONVERSATION_TOPIC:
		case PR_READ:
		case PR_INTERNET_MESSAGE_ID:
		case PR_INTERNET_REFERENCES:
		case PR_SENDER_ADDRTYPE:
		case PR_SENDER_EMAIL_ADDRESS:
		case PR_SENDER_NAME:
		case PR_SENT_REPRESENTING_ADDRTYPE:
		case PR_SENT_REPRESENTING_EMAIL_ADDRESS:
		case PR_SENT_REPRESENTING_NAME:
			continue;
		case PR_ASSOCIATED:
			IsAssociated.emplace(*pval(uint8_t)); break;
		case PR_CHANGE_KEY:
			ItemId? ItemId->ChangeKey = *tp : ItemId.emplace().ChangeKey.emplace(*tp); break;
		case PR_CLIENT_SUBMIT_TIME:
			DateTimeSent = sTimePoint::fromNT(*pval(uint64_t)); break;
		case PR_CONVERSATION_ID:
			ConversationId.emplace(*tp); break;
		case PR_DISPLAY_CC:
			DisplayCc.emplace(pval(char)); break;
		case PR_DISPLAY_BCC:
			DisplayBcc.emplace(pval(char)); break;
		case PR_DISPLAY_TO:
			DisplayTo.emplace(pval(char)); break;
		case PR_ENTRYID:
			ItemId? ItemId->Id = *tp : ItemId.emplace(*tp); break;
		case PR_HASATTACH:
			HasAttachments.emplace(*pval(uint8_t)); break;
		case PR_FLAG_STATUS:
			Flag.emplace().FlagStatus = *pval(uint32_t) == followupFlagged? Enum::Flagged :
			                            *pval(uint32_t) == followupComplete? Enum::Complete : Enum::NotFlagged;
			break;
		case PR_IMPORTANCE:
			Importance = *pval(uint32_t) == IMPORTANCE_LOW? Enum::Low :
							 *pval(uint32_t) == IMPORTANCE_HIGH? Enum::High : Enum::Normal;
			break;
		case PR_IN_REPLY_TO_ID:
			InReplyTo.emplace(pval(char)); break;
		case PR_LAST_MODIFIER_NAME:
			LastModifiedName.emplace(pval(char)); break;
		case PR_LAST_MODIFICATION_TIME:
			LastModifiedTime.emplace(nttime_to_time_point(*pval(uint64_t))); break;
		case PR_MESSAGE_CLASS:
			ItemClass.emplace(pval(char)); break;
		case PR_MESSAGE_DELIVERY_TIME:
			DateTimeReceived = sTimePoint::fromNT(*pval(uint64_t)); break;
		case PR_MESSAGE_SIZE_EXTENDED:
			Size.emplace(*pval(uint64_t)); break;
		case PR_PARENT_ENTRYID:
			ParentFolderId.emplace(*tp); break;
		case PR_SUBJECT:
			Subject.emplace(pval(char)); break;
		default:
			ExtendedProperty.emplace_back(*tp);
		}
}

#undef pval

sItem tItem::create(const TPROPVAL_ARRAY& itemProps, const sNamedPropertyMap& namedProps)
{
	const char* itemClass = itemProps.get<const char>(PR_MESSAGE_CLASS);
	if(!itemClass)
		return tItem(itemProps, namedProps);
	if(!strcasecmp(itemClass, "IPM.Note"))
		return tMessage(itemProps, namedProps);
	return tItem(itemProps, namedProps);
}

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief     Collect property tags and names for item shape

 * @param     tags    Inserter to use for property tags
 * @param     names   Inserter to use for property names
 * @param     types   Inserter to use for the type of each named property
 */
void tItemResponseShape::tags(vector_inserter<uint32_t>& tagIns, vector_inserter<PROPERTY_NAME>& nameIns,
                              vector_inserter<uint16_t>& typeIns) const
{
	for(uint32_t tag : tagsIdOnly)
		tagIns = tag;
	if(AdditionalProperties)
		for(const auto& additional : *AdditionalProperties)
			additional.tags(tagIns, nameIns, typeIns);
}

///////////////////////////////////////////////////////////////////////////////

#define pval(type) static_cast<const type*>(tp->pvalue)

tMessage::tMessage(const TPROPVAL_ARRAY& propvals, const sNamedPropertyMap& namedProps) : tItem(propvals, namedProps)
{
	for(const TAGGED_PROPVAL* tp = propvals.ppropval; tp < propvals.ppropval+propvals.count; ++tp)
		switch(tp->proptag)
		{
		case PR_CONVERSATION_INDEX:
			ConversationIndex = *tp; break;
		case PR_CONVERSATION_TOPIC:
		   ConversationTopic = pval(char); break;
		case PR_READ:
			IsRead = *pval(bool); break;
		case PR_INTERNET_MESSAGE_ID:
			InternetMessageId = pval(char); break;
		case PR_INTERNET_REFERENCES:
			References = pval(char); break;
		case PR_SENDER_ADDRTYPE:
			Sender? Sender->RoutingType = pval(char) : Sender.emplace().RoutingType = pval(char); break;
		case PR_SENDER_EMAIL_ADDRESS:
			Sender? Sender->EmailAddress = pval(char) : Sender.emplace().EmailAddress = pval(char); break;
		case PR_SENDER_NAME:
			Sender? Sender->Name = pval(char) : Sender.emplace().Name = pval(char); break;
		case PR_SENT_REPRESENTING_ADDRTYPE:
			From? From->RoutingType = pval(char) : From.emplace().RoutingType = pval(char); break;
		case PR_SENT_REPRESENTING_EMAIL_ADDRESS:
			From? From->EmailAddress = pval(char) : From.emplace().EmailAddress = pval(char); break;
		case PR_SENT_REPRESENTING_NAME:
			From? From->Name = pval(char) : From.emplace().Name = pval(char); break;
		default:
			break;
		}
}

#undef pval

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief     Collect property tags and names for path specification

 * @param     tags    Inserter to use for property tags
 * @param     names   Inserter to use for property names
 * @param     types   Inserter to use for the type of each named property
 */
void tPath::tags(vector_inserter<uint32_t>& tagIns, vector_inserter<PROPERTY_NAME>& nameIns,
                     vector_inserter<uint16_t>& typeIns) const
{return std::visit([&](auto&& v){return v.tags(tagIns, nameIns, typeIns);}, *static_cast<const Base*>(this));};

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief      Calculate time zone offset for time point
 *
 * @param      tp      Time point to calculate offset for
 *
 * @return     Offset in minutes
 */
std::chrono::minutes tSerializableTimeZone::offset(const time_point& tp) const
{
	time_t temp = time_point::clock::to_time_t(tp)-Bias*60;
	tm datetime;
	gmtime_r(&temp, &datetime);

	auto &first  = StandardTime.Month < DaylightTime.Month? StandardTime : DaylightTime;
	auto &second = StandardTime.Month < DaylightTime.Month? DaylightTime : StandardTime;

	int firstDO    = first.DayOrder == 5 ? -1 : int(first.DayOrder);
	int secondDO   = second.DayOrder == 5 ? -1 : int(second.DayOrder);
	int firstMday  = ical_get_dayofmonth(datetime.tm_year + 1900,
	                 first.Month, firstDO, int(first.DayOfWeek.index()));
	int secondMday = ical_get_dayofmonth(datetime.tm_year + 1900,
	                 second.Month, secondDO, int(second.DayOfWeek.index()));

	int64_t dStamp = int64_t(datetime.tm_sec) + datetime.tm_min * 60 +
	                 datetime.tm_hour * 3600 + datetime.tm_mday * 86400 +
	                 (datetime.tm_mon + 1) * 2678400;
	int64_t fStamp = int64_t(first.Time.second) + first.Time.minute * 60 +
	                 first.Time.hour * 3600 + firstMday * 86400 +
	                 first.Month * 2678400;
	int64_t sStamp = int64_t(second.Time.second) + second.Time.minute * 60 +
	                 second.Time.hour * 3600 + secondMday * 86400 +
	                 second.Month * 2678400;

	int bias = dStamp < fStamp || dStamp >= sStamp ? second.Bias : first.Bias;
	return std::chrono::minutes(Bias+bias);
}

/**
 * @brief      Convert from UTC to timezone
 *
 * @param      tp     Time point to convert
 *
 * @return     Adjusted time point
 */
gromox::time_point tSerializableTimeZone::apply(const gromox::time_point& tp) const
{return tp+offset(tp);}


/**
 * @brief      Convert from UTC to timezone
 *
 * @param      tp     Time point to convert
 *
 * @return     Adjusted time point
 */
gromox::time_point tSerializableTimeZone::remove(const gromox::time_point& tp) const
{return tp-offset(tp);}

///////////////////////////////////////////////////////////////////////////////

tSyncFolderHierarchyCU::tSyncFolderHierarchyCU(sFolder&& folder) : folder(folder)
{}

///////////////////////////////////////////////////////////////////////////////

tTargetFolderIdType::tTargetFolderIdType(std::variant<tFolderId, tDistinguishedFolderId>&& id) :
    folderId(std::move(id))
{}

///////////////////////////////////////////////////////////////////////////////

mFreeBusyResponse::mFreeBusyResponse(tFreeBusyView&& fbv) : FreeBusyView(std::move(fbv))
{}

///////////////////////////////////////////////////////////////////////////////

mResponseMessageType::mResponseMessageType(const std::string& ResponseClass, const std::optional<std::string>& ResponseCode,
                                           const std::optional<std::string>& MessageText) :
    ResponseClass(ResponseClass), MessageText(MessageText), ResponseCode(ResponseCode)
{}

/**
 * @brief      Set response data to success
 *
 * @return     *this
 */
mResponseMessageType& mResponseMessageType::success()
{
	ResponseClass = "Success";
	ResponseCode = "NoError";
	return *this;
}
