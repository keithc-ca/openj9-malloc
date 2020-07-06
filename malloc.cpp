/*******************************************************************************
 * Copyright (c) 2020, 2020 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gnu/lib-names.h> /* for LIBC_SO */

#define MALLOC_DEBUG 0

extern "C"
{
	typedef void * (* calloc_func)(size_t count, size_t size);
	typedef void   (* free_func)(void *data);
	typedef void * (* malloc_func)(size_t bytes);
	typedef void * (* realloc_func)(void * data, size_t bytes);
} /* extern "C" */

namespace
{

#define HEADER_MAGIC ((size_t)0x474F4F4D204D454EuLL) /* GOOD MEM */

struct BlockHeader
{
	size_t magic;
	size_t size;
};

#if MALLOC_DEBUG
extern "C" void
malloc_trap(BlockHeader * header)
{
	fprintf(stderr, "[DEBUG:malloc] trap on %p\n", header);
}
#endif

class MallocHelper
{
private:
	char * bootCursor;
	void * handle;
	calloc_func realCalloc;
	free_func realFree;
	malloc_func realMalloc;
	realloc_func realRealloc;

	/* A small amount of space is required by our call to dlopen() below. */
	enum { BootHeapSize = 0x1000 };

	char bootHeap[BootHeapSize];

	MallocHelper();
	~MallocHelper();

	size_t adjustSize(size_t bytes);
	void * recordSize(void * data, size_t bytes);
	void * preRelease(void * data);
	bool isInBootHeap(void * data) const;

public:
	static MallocHelper instance;

	void * callocImpl(size_t count, size_t size);
	void freeImpl(void * data);
	void * mallocImpl(size_t bytes);
	void * reallocImpl(void * data, size_t bytes);
};

MallocHelper MallocHelper::instance;

MallocHelper::MallocHelper()
	: bootCursor(bootHeap)
	, handle(NULL)
	, realCalloc(NULL)
	, realFree(NULL)
	, realMalloc(NULL)
	, realRealloc(NULL)
{
#if MALLOC_DEBUG
	fprintf(stderr, "[DEBUG:malloc] bootstrap heap @ %p\n", bootHeap);
#endif

	handle = dlopen(LIBC_SO, RTLD_LAZY | RTLD_LOCAL);
	if (NULL == handle) {
		fprintf(stderr, "could not open %s: %s\n", LIBC_SO, dlerror());
		return;
	}

	realCalloc = (calloc_func)dlsym(handle, "calloc");
	if (NULL == realCalloc) {
		fprintf(stderr, "could not lookup calloc\n");
		goto fail;
	}

	realFree = (free_func)dlsym(handle, "free");
	if (NULL == realFree) {
		fprintf(stderr, "could not lookup free\n");
		goto fail;
	}

	realMalloc = (malloc_func)dlsym(handle, "malloc");
	if (NULL == realMalloc) {
		fprintf(stderr, "could not lookup malloc\n");
		goto fail;
	}

	realRealloc = (realloc_func)dlsym(handle, "realloc");
	if (NULL == realRealloc) {
		fprintf(stderr, "could not lookup realloc\n");
fail:
		dlclose(handle);
		handle = NULL;
		realCalloc = NULL;
		realFree = NULL;
		realMalloc = NULL;
		realRealloc = NULL;
	} else {
#if MALLOC_DEBUG
		fprintf(stderr, "[DEBUG:malloc] %zu bytes of bootstrap heap used\n", bootCursor - bootHeap);
#endif
	}
}

MallocHelper::~MallocHelper()
{
	if (NULL != handle) {
		dlclose(handle);
		handle = NULL;
		realCalloc = NULL;
		realFree = NULL;
		realMalloc = NULL;
		realRealloc = NULL;
	}
}

size_t
MallocHelper::adjustSize(size_t bytes)
{
	/* Adjust the number of raw bytes to allow space for the header. */
	return sizeof(BlockHeader) + bytes;
}

void *
MallocHelper::recordSize(void * data, size_t bytes)
{
	if (NULL == data) {
		return NULL;
	}

	/*
	 * Save the originally requested size in a header and return
	 * a pointer to the space to be used by the requestor.
	 */
	BlockHeader * header = (BlockHeader *)data;

	header->magic = HEADER_MAGIC;
	header->size = bytes;

#if MALLOC_DEBUG
	fprintf(stderr, "[DEBUG:malloc] allocate %zu bytes @ %p\n", header->size, header + 1);
#endif

	return header + 1;
}

void *
MallocHelper::preRelease(void * data)
{
	if (NULL == data) {
		return NULL;
	}

	/*
	 * Adjust the pointer provided to the application so it
	 * refers to the raw allocation (including the added header).
	 */
	BlockHeader * header = (BlockHeader *)data;

	header -= 1;

#if MALLOC_DEBUG
	if (HEADER_MAGIC != header->magic) {
		malloc_trap(header);
	}

	fprintf(stderr, "[DEBUG:malloc] release %zu bytes @ %p\n", header->size, data);
#endif

	return header;
}

bool
MallocHelper::isInBootHeap(void * data) const
{
	char * test = (char *)data;

	return (test >= bootHeap) && ((test - bootHeap) < BootHeapSize);
}

void *
MallocHelper::callocImpl(size_t count, size_t size)
{
	if ((0 != count) && (0 != size) && (count > ((~(size_t)0) / size))) {
		/* size overflow */
		return NULL;
	}

	size_t bytes = count * size;
	size_t adjustedBytes = adjustSize(bytes);
	calloc_func allocator = realCalloc;

	if (NULL == allocator) {
#if MALLOC_DEBUG
		fprintf(stderr, "[DEBUG:calloc] can't satisfy early request for %zu bytes\n", bytes);
#endif
		return NULL;
	}

	void * result = NULL;
	result = allocator(adjustedBytes, 1);

	return recordSize(result, bytes);
}

void *
MallocHelper::mallocImpl(size_t bytes)
{
	void * result = NULL;
	size_t adjustedBytes = adjustSize(bytes);
	malloc_func allocator = realMalloc;

	if (NULL != allocator) {
		result = allocator(adjustedBytes);
	} else {
		/* bootstrap from our private heap area */
		size_t available = BootHeapSize - (bootCursor - bootHeap);

		if (available < adjustedBytes) {
#if MALLOC_DEBUG
			fprintf(stderr, "[DEBUG:malloc] can't satisfy request for %zu bytes from bootstrap heap\n", bytes);
#endif
			return NULL;
		}

		result = bootCursor;
		bootCursor += adjustedBytes;
	}

	return recordSize(result, bytes);
}

void *
MallocHelper::reallocImpl(void * data, size_t bytes)
{
	if (NULL == data) {
		/* free(NULL) does nothing */
		return mallocImpl(bytes);
	}

	realloc_func reallocator = realRealloc;

	if (NULL == reallocator) {
#if MALLOC_DEBUG
		fprintf(stderr, "[DEBUG:realloc] can't satisfy early request for %zu bytes\n", bytes);
#endif
		return NULL;
	}

	void * result = NULL;
	void * adjustedData = preRelease(data);
	size_t adjustedBytes = adjustSize(bytes);

	if (isInBootHeap(data)) {
		/* the block was originally allocated from our private heap */
		BlockHeader * header = (BlockHeader *)adjustedData;

		result = mallocImpl(bytes);

		if (NULL != result) {
			size_t oldSize = header->size;

			memcpy(result, data, bytes < oldSize ? bytes : oldSize);
		}
	} else {
		result = reallocator(adjustedData, adjustedBytes);
	}

	return recordSize(result, bytes);
}

void
MallocHelper::freeImpl(void * data)
{
	if (NULL == data) {
		/* free(NULL) does nothing */
	} else if (isInBootHeap(data)) {
		/* don't call real free for data allocated from our private heap */
	} else {
		free_func releaser = realFree;

		if (NULL != releaser) {
			void * adjustedData = preRelease(data);

			releaser(adjustedData);
		}
	}
}

} /* namespace */

extern "C"
{

void *
calloc(size_t count, size_t size)
{
	return MallocHelper::instance.callocImpl(count, size);
}

void
free(void *data)
{
	MallocHelper::instance.freeImpl(data);
}

void *
malloc(size_t bytes)
{
	return MallocHelper::instance.mallocImpl(bytes);
}

void *
realloc(void * data, size_t bytes)
{
	return MallocHelper::instance.reallocImpl(data, bytes);
}

} /* extern "C" */
