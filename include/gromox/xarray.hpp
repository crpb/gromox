#pragma once
#include <gromox/lib_buffer.hpp>
#include <gromox/double_list.hpp>
#define XARRAY_CACHEITEM_NUMBER  (16*1024)
#define XARRAY_HASHITEM_NUMBER   (4*1024)

struct XARRAY_UNIT {
	DOUBLE_LIST_NODE    node;
	DOUBLE_LIST_NODE    node_hash;
	int                 index;
	unsigned int        xtag;
};

/* the extra memory ocupation for xarray node */
#define EXTRA_XARRAYNODE_SIZE sizeof(XARRAY_UNIT)

struct GX_EXPORT XARRAY {
	int append(void *data, unsigned int xtag);
	void *get_item(size_t index);
	void *get_itemx(unsigned int xtag);
	inline size_t get_capacity() const { return cur_size; }
	void clear();

    LIB_BUFFER*  mbuf_pool;
    DOUBLE_LIST  mlist;
    size_t       data_size;
    size_t       cur_size;
    void*        cache_ptrs[XARRAY_CACHEITEM_NUMBER];
	DOUBLE_LIST  hash_lists[XARRAY_HASHITEM_NUMBER];
};

extern GX_EXPORT void xarray_init(XARRAY *, LIB_BUFFER *pool, size_t data_size);
void xarray_free(XARRAY* pxarray);
