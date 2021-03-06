/*
 * Copyright (C) 2004-2011  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: rdataslab.c,v 1.55 2011-12-20 00:55:01 marka Exp $ */

/*! \file */

#include <pthread.h>
#include <config.h>

#include <stdlib.h>
#include <assert.h>

#include <isc/mem.h>
#include <isc/region.h>
#include <isc/string.h>		/* Required for HP/UX (and others?) */
#include <isc/util.h>
#include <isc/rwlock.h>

#include <dns/result.h>
#include <dns/rdata.h>
#include <dns/rdataset.h>
#include <dns/rdataslab.h>
#include <dns/adb.h>

// #define LB_LOCKING

#ifdef LB_LOCKING
#define LB_RDLOCK(x) pthread_rwlock_rdlock(x)
#define LB_WRLOCK(x) pthread_rwlock_wrlock(x)
#define LB_UNLOCK(x) pthread_rwlock_unlock(x)
#define LB_INIT_LOCK(x) pthread_rwlock_init(x, NULL)
#else
#define LB_RDLOCK(x) 
#define LB_WRLOCK(x) 
#define LB_UNLOCK(x) 
#define LB_INIT_LOCK(x) 
#endif

/*
 * The rdataslab structure allows iteration to occur in both load order
 * and DNSSEC order.  The structure is as follows:
 *
 *	header		(reservelen bytes)
 *	record count	(2 bytes)
 *	offset table	(4 x record count bytes in load order)
 *	data records
 *		data length	(2 bytes)
 *		order		(2 bytes)
 *		meta data	(1 byte for RRSIG's)
 *		data		(data length bytes)
 *
 * If DNS_RDATASET_FIXED is defined to be zero (0) the format of a
 * rdataslab is as follows:
 *
 *	header		(reservelen bytes)
 *	record count	(2 bytes)
 *	data records
 *		data length	(2 bytes)
 *		meta data	(1 byte for RRSIG's)
 *		data		(data length bytes)
 *
 * Offsets are from the end of the header.
 *
 * Load order traversal is performed by walking the offset table to find
 * the start of the record (DNS_RDATASET_FIXED = 1).
 *
 * DNSSEC order traversal is performed by walking the data records.
 *
 * The order is stored with record to allow for efficient reconstruction
 * of the offset table following a merge or subtraction.
 *
 * The iterator methods here currently only support DNSSEC order iteration.
 *
 * The iterator methods in rbtdb support both load order and DNSSEC order
 * iteration.
 *
 * WARNING:
 *	rbtdb.c directly interacts with the slab's raw structures.  If the
 *	structure changes then rbtdb.c also needs to be updated to reflect
 *	the changes.  See the areas tagged with "RDATASLAB".
 */

struct xrdata {
	dns_rdata_t	rdata;
	unsigned int	order;
};

/*% Note: the "const void *" are just to make qsort happy.  */
static int
compare_rdata(const void *p1, const void *p2) {
	const struct xrdata *x1 = p1;
	const struct xrdata *x2 = p2;
	return (dns_rdata_compare(&x1->rdata, &x2->rdata));
}

#if DNS_RDATASET_FIXED
static void
fillin_offsets(unsigned char *offsetbase, unsigned int *offsettable,
	       unsigned length)
{
	unsigned int i, j;
	unsigned char *raw;

	for (i = 0, j = 0; i < length; i++) {

		if (offsettable[i] == 0)
			continue;

		/*
		 * Fill in offset table.
		 */
		raw = &offsetbase[j*4 + 2];
		*raw++ = (offsettable[i] & 0xff000000) >> 24;
		*raw++ = (offsettable[i] & 0xff0000) >> 16;
		*raw++ = (offsettable[i] & 0xff00) >> 8;
		*raw = offsettable[i] & 0xff;

		/*
		 * Fill in table index.
		 */
		raw = offsetbase + offsettable[i] + 2;
//     fprintf(stderr, "base=%p raw1=%p offset=%lu\n", offsetbase, raw, offsettable[i]);
		*raw++ = (j & 0xff00) >> 8;
		*raw = j++ & 0xff;
	}
}
#endif

isc_result_t
dns_rdataslab_fromrdataset(dns_rdataset_t *rdataset, isc_mem_t *mctx,
			   isc_region_t *region, unsigned int reservelen)
{
	struct xrdata  *x;
	unsigned char  *rawbuf;
#if DNS_RDATASET_FIXED
	unsigned char  *offsetbase;
#endif
	unsigned int	buflen;
	isc_result_t	result;
	unsigned int	nitems;
	unsigned int	nalloc;
	unsigned int	i;
#if DNS_RDATASET_FIXED
	unsigned int   *offsettable;
#endif
	unsigned int	length;

	buflen = reservelen + 2;

	nalloc = dns_rdataset_count(rdataset);
	nitems = nalloc;
	if (nitems == 0 && rdataset->type != 0)
		return (ISC_R_FAILURE);

	if (nalloc > 0xffff)
		return (ISC_R_NOSPACE);


	if (nalloc != 0) {
		x = isc_mem_get(mctx, nalloc * sizeof(struct xrdata));
		if (x == NULL)
			return (ISC_R_NOMEMORY);
	} else
		x = NULL;

	/*
	 * Save all of the rdata members into an array.
	 */
	result = dns_rdataset_first(rdataset);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOMORE)
		goto free_rdatas;
	for (i = 0; i < nalloc && result == ISC_R_SUCCESS; i++) {
		INSIST(result == ISC_R_SUCCESS);
		dns_rdata_init(&x[i].rdata);
		dns_rdataset_current(rdataset, &x[i].rdata);
#if DNS_RDATASET_FIXED
		x[i].order = i;
#endif
		result = dns_rdataset_next(rdataset);
	}
	if (result != ISC_R_NOMORE)
		goto free_rdatas;
	if (i != nalloc) {
		/*
		 * Somehow we iterated over fewer rdatas than
		 * dns_rdataset_count() said there were!
		 */
		result = ISC_R_FAILURE;
		goto free_rdatas;
	}

	/*
	 * Put into DNSSEC order.
	 */
	qsort(x, nalloc, sizeof(struct xrdata), compare_rdata);

	/*
	 * Remove duplicates and compute the total storage required.
	 *
	 * If an rdata is not a duplicate, accumulate the storage size
	 * required for the rdata.  We do not store the class, type, etc,
	 * just the rdata, so our overhead is 2 bytes for the number of
	 * records, and 8 for each rdata, (length(2), offset(4) and order(2))
	 * and then the rdata itself.
	 */
	for (i = 1; i < nalloc; i++) {
		if (compare_rdata(&x[i-1].rdata, &x[i].rdata) == 0) {
			x[i-1].rdata.data = NULL;
			x[i-1].rdata.length = 0;
#if DNS_RDATASET_FIXED
			/*
			 * Preserve the least order so A, B, A -> A, B
			 * after duplicate removal.
			 */
			if (x[i-1].order < x[i].order)
				x[i].order = x[i-1].order;
#endif
			nitems--;
		} else {
#if DNS_RDATASET_FIXED
			buflen += (8 + x[i-1].rdata.length);
#else
			buflen += (2 + x[i-1].rdata.length);
#endif
			/*
			 * Provide space to store the per RR meta data.
			 */
			if (rdataset->type == dns_rdatatype_rrsig)
				buflen++;
		}
	}
	/*
	 * Don't forget the last item!
	 */
	if (nalloc != 0) {
#if DNS_RDATASET_FIXED
		buflen += (8 + x[i-1].rdata.length);
#else
		buflen += (2 + x[i-1].rdata.length);
#endif
	}

	/*
	 * Provide space to store the per RR meta data.
	 */
	if (rdataset->type == dns_rdatatype_rrsig)
		buflen++;

	/*
	 * Ensure that singleton types are actually singletons.
	 */
	if (nitems > 1 && dns_rdatatype_issingleton(rdataset->type)) {
		/*
		 * We have a singleton type, but there's more than one
		 * RR in the rdataset.
		 */
		result = DNS_R_SINGLETON;
		goto free_rdatas;
	}

	/*
	 * Allocate the memory, set up a buffer, start copying in
	 * data.
	 */
	rawbuf = isc_mem_get(mctx, buflen);
	if (rawbuf == NULL) {
		result = ISC_R_NOMEMORY;
		goto free_rdatas;
	}

#if DNS_RDATASET_FIXED
	/* Allocate temporary offset table. */
	offsettable = isc_mem_get(mctx, nalloc * sizeof(unsigned int));
	if (offsettable == NULL) {
		isc_mem_put(mctx, rawbuf, buflen);
		result = ISC_R_NOMEMORY;
		goto free_rdatas;
	}
	memset(offsettable, 0, nalloc * sizeof(unsigned int));
#endif

	region->base = rawbuf;
	region->length = buflen;

	rawbuf += reservelen;
#if DNS_RDATASET_FIXED
	offsetbase = rawbuf;
#endif

	*rawbuf++ = (nitems & 0xff00) >> 8;
	*rawbuf++ = (nitems & 0x00ff);

#if DNS_RDATASET_FIXED
	/* Skip load order table.  Filled in later. */
	rawbuf += nitems * 4;
#endif

	for (i = 0; i < nalloc; i++) {
		if (x[i].rdata.data == NULL)
			continue;
#if DNS_RDATASET_FIXED
		offsettable[x[i].order] = rawbuf - offsetbase;
#endif
		length = x[i].rdata.length;
		if (rdataset->type == dns_rdatatype_rrsig)
			length++;
		*rawbuf++ = (length & 0xff00) >> 8;
		*rawbuf++ = (length & 0x00ff);
#if DNS_RDATASET_FIXED
		rawbuf += 2;	/* filled in later */
#endif
		/*
		 * Store the per RR meta data.
		 */
		if (rdataset->type == dns_rdatatype_rrsig) {
			*rawbuf++ |= (x[i].rdata.flags & DNS_RDATA_OFFLINE) ?
					    DNS_RDATASLAB_OFFLINE : 0;
		}
		memcpy(rawbuf, x[i].rdata.data, x[i].rdata.length);
		rawbuf += x[i].rdata.length;
	}

#if DNS_RDATASET_FIXED
	fillin_offsets(offsetbase, offsettable, nalloc);
	isc_mem_put(mctx, offsettable, nalloc * sizeof(unsigned int));
#endif

	result = ISC_R_SUCCESS;

 free_rdatas:
	if (x != NULL)
		isc_mem_put(mctx, x, nalloc * sizeof(struct xrdata));
	return (result);
}

static void
rdataset_disassociate(dns_rdataset_t *rdataset) {
	UNUSED(rdataset);
}

// ZAKKAK
// changed to use the offsets
static isc_result_t
rdataset_first(dns_rdataset_t *rdataset) {
  unsigned char *raw;
  unsigned int count;
  unsigned int offset;

  LB_RDLOCK(rdataset->private1);
  
  raw = rdataset->private3;
  
// 	count = raw[0] * 256 + raw[1];
  count = raw[0] << 8 | raw[1];
	if (count == 0) {
		rdataset->private5 = NULL;
		return (ISC_R_NOMORE);
	}
  offset = (raw[2] << 24) | (raw[3] << 16) | (raw[4] << 8) | (raw[5]);
//   fprintf(stderr, "Index1=0 offset=%lu base=%p\n", offset, raw);
// #if DNS_RDATASET_FIXED
// 	raw += 2 + (4 * count);
// #else
// 	raw += 2;
// #endif
  
	/*
	 * The privateuint4 field is the number of rdata beyond the cursor
	 * position, so we decrement the total count by one before storing
	 * it.
	 */
	count--;
	rdataset->privateuint4 = count;
//   rdataset->private5 = raw;
  rdataset->private5 = raw + offset;
  
  LB_UNLOCK(rdataset->private1);

	return (ISC_R_SUCCESS);
}

// ZAKKAK
// changed to use the offsets
static isc_result_t
rdataset_next(dns_rdataset_t *rdataset) {
  unsigned int count;
// 	unsigned int length;
  unsigned int orig_count;
  unsigned int current;
  unsigned int offset;
  unsigned char *raw;

  LB_RDLOCK(rdataset->private1);

  count = rdataset->privateuint4;
  if (count == 0)
    return (ISC_R_NOMORE);
  raw = rdataset->private3;
  orig_count = raw[0] << 8 | raw[1];
  current = orig_count - count;
  count--;
  rdataset->privateuint4 = count;
  raw += 2 + current*4;
  offset = (raw[0] << 24) | (raw[1] << 16) | (raw[2] << 8) | (raw[3]);
  
  rdataset->private5 = (char*)rdataset->private3 + offset;
  
  LB_UNLOCK(rdataset->private1);

	return (ISC_R_SUCCESS);
}

static void
rdataset_current(dns_rdataset_t *rdataset, dns_rdata_t *rdata) {
	unsigned char *raw;
	isc_region_t r;
	unsigned int length;
	unsigned int flags = 0;
  
//   printf("KKK\n");
  LB_RDLOCK(rdataset->private1);
//   printf("LLL\n");
  raw = rdataset->private5;

	REQUIRE(raw != NULL);

  length = raw[0] * 256 + raw[1];
  unsigned int index = raw[2] * 256 + raw[3];
//   fprintf(stderr, "Index=%lu length=%lu raw=%p base=%p\n", index, length, &raw[2], rdataset->private3);
#if DNS_RDATASET_FIXED
	raw += 4;
#else
	raw += 2;
#endif
	if (rdataset->type == dns_rdatatype_rrsig) {
		if (*raw & DNS_RDATASLAB_OFFLINE)
			flags |= DNS_RDATA_OFFLINE;
		length--;
		raw++;
	}
	r.length = length;
	r.base = raw;
	dns_rdata_fromregion(rdata, rdataset->rdclass, rdataset->type, &r);
	rdata->flags |= flags;
  
  LB_UNLOCK(rdataset->private1);
}

static void
rdataset_clone(dns_rdataset_t *source, dns_rdataset_t *target) {
  
  // FIXME: possible deadlock?
  LB_RDLOCK(source->private1);
  LB_WRLOCK(target->private1);
	
  *target = *source;

	/*
	 * Reset iterator state.
	 */
	target->privateuint4 = 0;
	target->private5 = NULL;
  
  LB_UNLOCK(target->private1);
  LB_UNLOCK(source->private1);
}

static unsigned int
rdataset_count(dns_rdataset_t *rdataset) {
	unsigned char *raw;
	unsigned int count;
  
  LB_RDLOCK(rdataset->private1);

  raw = rdataset->private3;
  
	count = raw[0] * 256 + raw[1];
  
  LB_UNLOCK(rdataset->private1);

	return (count);
}

static dns_rdatasetmethods_t rdataset_methods = {
	rdataset_disassociate,
	rdataset_first,
	rdataset_next,
	rdataset_current,
	rdataset_clone,
	rdataset_count,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

void
dns_rdataslab_tordataset(unsigned char *slab, unsigned int reservelen,
			 dns_rdataclass_t rdclass, dns_rdatatype_t rdtype,
			 dns_rdatatype_t covers, dns_ttl_t ttl,
			 dns_rdataset_t *rdataset)
{
	REQUIRE(slab != NULL);
	REQUIRE(!dns_rdataset_isassociated(rdataset));

	rdataset->methods = &rdataset_methods;
	rdataset->rdclass = rdclass;
	rdataset->type = rdtype;
	rdataset->covers = covers;
	rdataset->ttl = ttl;
	rdataset->trust = 0;
	rdataset->private1 = NULL;
	rdataset->private2 = NULL;
	rdataset->private3 = slab + reservelen;

	/*
	 * Reset iterator state.
	 */
	rdataset->privateuint4 = 0;
	rdataset->private5 = NULL;
}

#if DNS_RDATASET_FIXED
//ZAKKAK lets create a sorter
// struct zakkak_off {
//   unsigned int offset;
//   unsigned int order;
// };
// 
// static int
// cmp(const void *p1, const void *p2) {
//   const struct zakkak_off *x1 = p1;
//   const struct zakkak_off *x2 = p2;
//   return (x1->order - x2->order);
// }

isc_result_t
dns_rdataslab_sort_fromrdataset(dns_rdataset_t *rdataset, ns_profiler_a_node_t *addr_stats)
{
//   struct zakkak_off  *x;
  unsigned char  *raw, *raw2;
  unsigned char  *base;
  isc_result_t  result = ISC_R_SUCCESS;
  unsigned int  nitems;
  unsigned int  i;

  assert(addr_stats);
  
//   printf("AAA\n");
  LB_WRLOCK(rdataset->private1);
//   printf("BBB\n");
  
  base = raw = rdataset->private3;
  // the first two bytes are the number of items
  nitems =  raw[0] << 8 | raw[1];
  raw += 2;

  // point raw to the start of the offsettable
  raw = base + 2;
  for (i = 0; i < nitems; i++) {
    // fill offsettable[i]
    *raw++ = (addr_stats[i].offset & 0xff000000) >> 24;
    *raw++ = (addr_stats[i].offset & 0xff0000) >> 16;
    *raw++ = (addr_stats[i].offset & 0xff00) >> 8;
    *raw++ = addr_stats[i].offset & 0xff;
  
    // Update the table index for each rdata
    raw2 = base + addr_stats[i].offset + 2;
    *raw2++ = (i & 0xff00) >> 8;
    *raw2++ = i & 0xff;
  }

  result = ISC_R_SUCCESS;
  
  /*
   * Reset iterator state.
   */
  rdataset->privateuint4 = 0;
  rdataset->private5 = NULL;
  
  LB_UNLOCK(rdataset->private1);

//   free(x);
  return result;
}

isc_result_t
dns_rdataslab_transformrdataset(dns_rdataset_t *rdataset, ns_profiler_a_node_t *addr_stats)
{
  struct xrdata  *x;
  unsigned char  *rawbuf;
  unsigned char  *offsetbase;
  isc_result_t  result = ISC_R_SUCCESS;
  unsigned int  nitems;
  unsigned int  i;
  unsigned int   *offsettable;
  unsigned int  length;
  dns_rdata_in_a_t rdata_a;
  dns_rdata_t tmp_rdata;

  nitems = dns_rdataset_count(rdataset);

  if (nitems > 0) {
    x = malloc(nitems * sizeof(struct xrdata));
  } else
    return result;

  /*
   * Save all of the rdata members into an array.
   */
  result = dns_rdataset_first(rdataset);
  if (result != ISC_R_SUCCESS && result != ISC_R_NOMORE)
    goto free_rdatas;

  for (i = 0; i < nitems && result == ISC_R_SUCCESS; i++) {
    INSIST(result == ISC_R_SUCCESS);
    dns_rdata_init(&tmp_rdata);
    dns_rdataset_current(rdataset, &tmp_rdata);

    x[i].rdata.data = malloc(tmp_rdata.length);

    memcpy(x[i].rdata.data, tmp_rdata.data, tmp_rdata.length);
    x[i].rdata.length = tmp_rdata.length;
    x[i].rdata.rdclass = tmp_rdata.rdclass;
    x[i].rdata.type = tmp_rdata.type;
    x[i].rdata.flags = tmp_rdata.flags;
    x[i].order = i;

    result = dns_rdataset_next(rdataset);
  }

  if (result != ISC_R_NOMORE)
    goto free_rdatas;
  
  if (i != nitems) {
    /*
     * Somehow we iterated over fewer rdatas than
     * dns_rdataset_count() said there were!
     */
    result = ISC_R_FAILURE;
    goto free_rdatas;
  }

  /*
   * Allocate the memory, set up a buffer, start copying in
   * data.
   */
  rawbuf = rdataset->private3;

  /* Allocate temporary offset table. */
  offsettable = malloc(nitems * sizeof(unsigned int));
  memset(offsettable, 0, nitems * sizeof(unsigned int));

  offsetbase = rawbuf;

  *rawbuf++ = (nitems & 0xff00) >> 8;
  *rawbuf++ = (nitems & 0x00ff);

  /* Skip load order table.  Filled in later. */
  rawbuf += nitems * 4;

  for (i = 0; i < nitems; i++) {
    if (x[i].rdata.data == NULL)
      continue;
    
    offsettable[i] = rawbuf - offsetbase;
    addr_stats[i].offset = offsettable[i];
    length = x[i].rdata.length;
    
    if (rdataset->type == dns_rdatatype_rrsig)
      length++;
    
    *rawbuf++ = (length & 0xff00) >> 8;
    *rawbuf++ = (length & 0x00ff);
    rawbuf += 2;  /* filled in later */
    
    /*
     * Store the per RR meta data.
     */
    if (rdataset->type == dns_rdatatype_rrsig) {
      *rawbuf++ |= (x[i].rdata.flags & DNS_RDATA_OFFLINE) ?
              DNS_RDATASLAB_OFFLINE : 0;
    }
    
    memcpy(rawbuf, x[i].rdata.data, x[i].rdata.length);
    free(x[i].rdata.data);
    rawbuf += x[i].rdata.length;
  }

  fillin_offsets(offsetbase, offsettable, nitems);
  free(offsettable);

  result = ISC_R_SUCCESS;

  /* TODOZ
   * Set TTL (dns_ttl_t)
   * typedef isc_uint32_t dns_ttl_t;
   * s ms ns? what?
   */
//   rdataset->ttl = UPDATE_INTERVAL*something;

  /*
   * set the methods for rdataslab
   */
  rdataset->methods = &rdataset_methods;

//   fprintf(stderr, "first1=%p\n", rdataset->methods->first);
  /*
   * Reset iterator state.
   */
  rdataset->privateuint4 = 0;
  rdataset->private5 = NULL;
  
#ifdef LB_LOCKING
  /* 
   * use private1 as rwlock
   */
  rdataset->private1 = malloc(sizeof(pthread_rwlock_t));
  LB_INIT_LOCK( rdataset->private1);
#endif
  
 free_rdatas:
  if (x != NULL)
    free(x);
  
  return result;
}
#endif

unsigned int
dns_rdataslab_size(unsigned char *slab, unsigned int reservelen) {
	unsigned int count, length;
	unsigned char *current;

	REQUIRE(slab != NULL);

	current = slab + reservelen;
	count = *current++ * 256;
	count += *current++;
#if DNS_RDATASET_FIXED
	current += (4 * count);
#endif
	while (count > 0) {
		count--;
		length = *current++ * 256;
		length += *current++;
#if DNS_RDATASET_FIXED
		current += length + 2;
#else
		current += length;
#endif
	}

	return ((unsigned int)(current - slab));
}

/*
 * Make the dns_rdata_t 'rdata' refer to the slab item
 * beginning at '*current', which is part of a slab of type
 * 'type' and class 'rdclass', and advance '*current' to
 * point to the next item in the slab.
 */
static inline void
rdata_from_slab(unsigned char **current,
	      dns_rdataclass_t rdclass, dns_rdatatype_t type,
	      dns_rdata_t *rdata)
{
	unsigned char *tcurrent = *current;
	isc_region_t region;
	unsigned int length;
	isc_boolean_t offline = ISC_FALSE;

	length = *tcurrent++ * 256;
	length += *tcurrent++;

	if (type == dns_rdatatype_rrsig) {
		if ((*tcurrent & DNS_RDATASLAB_OFFLINE) != 0)
			offline = ISC_TRUE;
		length--;
		tcurrent++;
	}
	region.length = length;
#if DNS_RDATASET_FIXED
	tcurrent += 2;
#endif
	region.base = tcurrent;
	tcurrent += region.length;
	dns_rdata_fromregion(rdata, rdclass, type, &region);
	if (offline)
		rdata->flags |= DNS_RDATA_OFFLINE;
	*current = tcurrent;
}

/*
 * Return true iff 'slab' (slab data of type 'type' and class 'rdclass')
 * contains an rdata identical to 'rdata'.  This does case insensitive
 * comparisons per DNSSEC.
 */
static inline isc_boolean_t
rdata_in_slab(unsigned char *slab, unsigned int reservelen,
	      dns_rdataclass_t rdclass, dns_rdatatype_t type,
	      dns_rdata_t *rdata)
{
	unsigned int count, i;
	unsigned char *current;
	dns_rdata_t trdata = DNS_RDATA_INIT;
	int n;

	current = slab + reservelen;
	count = *current++ * 256;
	count += *current++;

#if DNS_RDATASET_FIXED
	current += (4 * count);
#endif

	for (i = 0; i < count; i++) {
		rdata_from_slab(&current, rdclass, type, &trdata);

		n = dns_rdata_compare(&trdata, rdata);
		if (n == 0)
			return (ISC_TRUE);
		if (n > 0)	/* In DNSSEC order. */
			break;
		dns_rdata_reset(&trdata);
	}
	return (ISC_FALSE);
}

isc_result_t
dns_rdataslab_merge(unsigned char *oslab, unsigned char *nslab,
		    unsigned int reservelen, isc_mem_t *mctx,
		    dns_rdataclass_t rdclass, dns_rdatatype_t type,
		    unsigned int flags, unsigned char **tslabp)
{
	unsigned char *ocurrent, *ostart, *ncurrent, *tstart, *tcurrent, *data;
	unsigned int ocount, ncount, count, olength, tlength, tcount, length;
	dns_rdata_t ordata = DNS_RDATA_INIT;
	dns_rdata_t nrdata = DNS_RDATA_INIT;
	isc_boolean_t added_something = ISC_FALSE;
	unsigned int oadded = 0;
	unsigned int nadded = 0;
	unsigned int nncount = 0;
#if DNS_RDATASET_FIXED
	unsigned int oncount;
	unsigned int norder = 0;
	unsigned int oorder = 0;
	unsigned char *offsetbase;
	unsigned int *offsettable;
#endif

	/*
	 * XXX  Need parameter to allow "delete rdatasets in nslab" merge,
	 * or perhaps another merge routine for this purpose.
	 */

	REQUIRE(tslabp != NULL && *tslabp == NULL);
	REQUIRE(oslab != NULL && nslab != NULL);

	ocurrent = oslab + reservelen;
	ocount = *ocurrent++ * 256;
	ocount += *ocurrent++;
#if DNS_RDATASET_FIXED
	ocurrent += (4 * ocount);
#endif
	ostart = ocurrent;
	ncurrent = nslab + reservelen;
	ncount = *ncurrent++ * 256;
	ncount += *ncurrent++;
#if DNS_RDATASET_FIXED
	ncurrent += (4 * ncount);
#endif
	INSIST(ocount > 0 && ncount > 0);

#if DNS_RDATASET_FIXED
	oncount = ncount;
#endif

	/*
	 * Yes, this is inefficient!
	 */

	/*
	 * Figure out the length of the old slab's data.
	 */
	olength = 0;
	for (count = 0; count < ocount; count++) {
		length = *ocurrent++ * 256;
		length += *ocurrent++;
#if DNS_RDATASET_FIXED
		olength += length + 8;
		ocurrent += length + 2;
#else
		olength += length + 2;
		ocurrent += length;
#endif
	}

	/*
	 * Start figuring out the target length and count.
	 */
	tlength = reservelen + 2 + olength;
	tcount = ocount;

	/*
	 * Add in the length of rdata in the new slab that aren't in
	 * the old slab.
	 */
	do {
		dns_rdata_init(&nrdata);
		rdata_from_slab(&ncurrent, rdclass, type, &nrdata);
		if (!rdata_in_slab(oslab, reservelen, rdclass, type, &nrdata))
		{
			/*
			 * This rdata isn't in the old slab.
			 */
#if DNS_RDATASET_FIXED
			tlength += nrdata.length + 8;
#else
			tlength += nrdata.length + 2;
#endif
			if (type == dns_rdatatype_rrsig)
				tlength++;
			tcount++;
			nncount++;
			added_something = ISC_TRUE;
		}
		ncount--;
	} while (ncount > 0);
	ncount = nncount;

	if (((flags & DNS_RDATASLAB_EXACT) != 0) &&
	    (tcount != ncount + ocount))
		return (DNS_R_NOTEXACT);

	if (!added_something && (flags & DNS_RDATASLAB_FORCE) == 0)
		return (DNS_R_UNCHANGED);

	/*
	 * Ensure that singleton types are actually singletons.
	 */
	if (tcount > 1 && dns_rdatatype_issingleton(type)) {
		/*
		 * We have a singleton type, but there's more than one
		 * RR in the rdataset.
		 */
		return (DNS_R_SINGLETON);
	}

	if (tcount > 0xffff)
		return (ISC_R_NOSPACE);

	/*
	 * Copy the reserved area from the new slab.
	 */
	tstart = isc_mem_get(mctx, tlength);
	if (tstart == NULL)
		return (ISC_R_NOMEMORY);
	memcpy(tstart, nslab, reservelen);
	tcurrent = tstart + reservelen;
#if DNS_RDATASET_FIXED
	offsetbase = tcurrent;
#endif

	/*
	 * Write the new count.
	 */
	*tcurrent++ = (tcount & 0xff00) >> 8;
	*tcurrent++ = (tcount & 0x00ff);

#if DNS_RDATASET_FIXED
	/*
	 * Skip offset table.
	 */
	tcurrent += (tcount * 4);

	offsettable = isc_mem_get(mctx,
				  (ocount + oncount) * sizeof(unsigned int));
	if (offsettable == NULL) {
		isc_mem_put(mctx, tstart, tlength);
		return (ISC_R_NOMEMORY);
	}
	memset(offsettable, 0, (ocount + oncount) * sizeof(unsigned int));
#endif

	/*
	 * Merge the two slabs.
	 */
	ocurrent = ostart;
	INSIST(ocount != 0);
#if DNS_RDATASET_FIXED
	oorder = ocurrent[2] * 256 + ocurrent[3];
	INSIST(oorder < ocount);
#endif
	rdata_from_slab(&ocurrent, rdclass, type, &ordata);

	ncurrent = nslab + reservelen + 2;
#if DNS_RDATASET_FIXED
	ncurrent += (4 * oncount);
#endif

	if (ncount > 0) {
		do {
			dns_rdata_reset(&nrdata);
#if DNS_RDATASET_FIXED
			norder = ncurrent[2] * 256 + ncurrent[3];

			INSIST(norder < oncount);
#endif
			rdata_from_slab(&ncurrent, rdclass, type, &nrdata);
		} while (rdata_in_slab(oslab, reservelen, rdclass,
				       type, &nrdata));
	}

	while (oadded < ocount || nadded < ncount) {
		isc_boolean_t fromold;
		if (oadded == ocount)
			fromold = ISC_FALSE;
		else if (nadded == ncount)
			fromold = ISC_TRUE;
		else
			fromold = ISC_TF(compare_rdata(&ordata, &nrdata) < 0);
		if (fromold) {
#if DNS_RDATASET_FIXED
			offsettable[oorder] = tcurrent - offsetbase;
#endif
			length = ordata.length;
			data = ordata.data;
			if (type == dns_rdatatype_rrsig) {
				length++;
				data--;
			}
			*tcurrent++ = (length & 0xff00) >> 8;
			*tcurrent++ = (length & 0x00ff);
#if DNS_RDATASET_FIXED
			tcurrent += 2;	/* fill in later */
#endif
			memcpy(tcurrent, data, length);
			tcurrent += length;
			oadded++;
			if (oadded < ocount) {
				dns_rdata_reset(&ordata);
#if DNS_RDATASET_FIXED
				oorder = ocurrent[2] * 256 + ocurrent[3];
				INSIST(oorder < ocount);
#endif
				rdata_from_slab(&ocurrent, rdclass, type,
						&ordata);
			}
		} else {
#if DNS_RDATASET_FIXED
			offsettable[ocount + norder] = tcurrent - offsetbase;
#endif
			length = nrdata.length;
			data = nrdata.data;
			if (type == dns_rdatatype_rrsig) {
				length++;
				data--;
			}
			*tcurrent++ = (length & 0xff00) >> 8;
			*tcurrent++ = (length & 0x00ff);
#if DNS_RDATASET_FIXED
			tcurrent += 2;	/* fill in later */
#endif
			memcpy(tcurrent, data, length);
			tcurrent += length;
			nadded++;
			if (nadded < ncount) {
				do {
					dns_rdata_reset(&nrdata);
#if DNS_RDATASET_FIXED
					norder = ncurrent[2] * 256 + ncurrent[3];
					INSIST(norder < oncount);
#endif
					rdata_from_slab(&ncurrent, rdclass,
							type, &nrdata);
				} while (rdata_in_slab(oslab, reservelen,
						       rdclass, type,
						       &nrdata));
			}
		}
	}

#if DNS_RDATASET_FIXED
	fillin_offsets(offsetbase, offsettable, ocount + oncount);

	isc_mem_put(mctx, offsettable,
		    (ocount + oncount) * sizeof(unsigned int));
#endif

	INSIST(tcurrent == tstart + tlength);

	*tslabp = tstart;

	return (ISC_R_SUCCESS);
}

isc_result_t
dns_rdataslab_subtract(unsigned char *mslab, unsigned char *sslab,
		       unsigned int reservelen, isc_mem_t *mctx,
		       dns_rdataclass_t rdclass, dns_rdatatype_t type,
		       unsigned int flags, unsigned char **tslabp)
{
	unsigned char *mcurrent, *sstart, *scurrent, *tstart, *tcurrent;
	unsigned int mcount, scount, rcount ,count, tlength, tcount, i;
	dns_rdata_t srdata = DNS_RDATA_INIT;
	dns_rdata_t mrdata = DNS_RDATA_INIT;
#if DNS_RDATASET_FIXED
	unsigned char *offsetbase;
	unsigned int *offsettable;
	unsigned int order;
#endif

	REQUIRE(tslabp != NULL && *tslabp == NULL);
	REQUIRE(mslab != NULL && sslab != NULL);

	mcurrent = mslab + reservelen;
	mcount = *mcurrent++ * 256;
	mcount += *mcurrent++;
	scurrent = sslab + reservelen;
	scount = *scurrent++ * 256;
	scount += *scurrent++;
	INSIST(mcount > 0 && scount > 0);

	/*
	 * Yes, this is inefficient!
	 */

	/*
	 * Start figuring out the target length and count.
	 */
	tlength = reservelen + 2;
	tcount = 0;
	rcount = 0;

#if DNS_RDATASET_FIXED
	mcurrent += 4 * mcount;
	scurrent += 4 * scount;
#endif
	sstart = scurrent;

	/*
	 * Add in the length of rdata in the mslab that aren't in
	 * the sslab.
	 */
	for (i = 0; i < mcount; i++) {
		unsigned char *mrdatabegin = mcurrent;
		rdata_from_slab(&mcurrent, rdclass, type, &mrdata);
		scurrent = sstart;
		for (count = 0; count < scount; count++) {
			dns_rdata_reset(&srdata);
			rdata_from_slab(&scurrent, rdclass, type, &srdata);
			if (dns_rdata_compare(&mrdata, &srdata) == 0)
				break;
		}
		if (count == scount) {
			/*
			 * This rdata isn't in the sslab, and thus isn't
			 * being subtracted.
			 */
			tlength += mcurrent - mrdatabegin;
			tcount++;
		} else
			rcount++;
		dns_rdata_reset(&mrdata);
	}

#if DNS_RDATASET_FIXED
	tlength += (4 * tcount);
#endif

	/*
	 * Check that all the records originally existed.  The numeric
	 * check only works as rdataslabs do not contain duplicates.
	 */
	if (((flags & DNS_RDATASLAB_EXACT) != 0) && (rcount != scount))
		return (DNS_R_NOTEXACT);

	/*
	 * Don't continue if the new rdataslab would be empty.
	 */
	if (tcount == 0)
		return (DNS_R_NXRRSET);

	/*
	 * If nothing is going to change, we can stop.
	 */
	if (rcount == 0)
		return (DNS_R_UNCHANGED);

	/*
	 * Copy the reserved area from the mslab.
	 */
	tstart = isc_mem_get(mctx, tlength);
	if (tstart == NULL)
		return (ISC_R_NOMEMORY);
	memcpy(tstart, mslab, reservelen);
	tcurrent = tstart + reservelen;
#if DNS_RDATASET_FIXED
	offsetbase = tcurrent;

	offsettable = isc_mem_get(mctx, mcount * sizeof(unsigned int));
	if (offsettable == NULL) {
		isc_mem_put(mctx, tstart, tlength);
		return (ISC_R_NOMEMORY);
	}
	memset(offsettable, 0, mcount * sizeof(unsigned int));
#endif

	/*
	 * Write the new count.
	 */
	*tcurrent++ = (tcount & 0xff00) >> 8;
	*tcurrent++ = (tcount & 0x00ff);

#if DNS_RDATASET_FIXED
	tcurrent += (4 * tcount);
#endif

	/*
	 * Copy the parts of mslab not in sslab.
	 */
	mcurrent = mslab + reservelen;
	mcount = *mcurrent++ * 256;
	mcount += *mcurrent++;
#if DNS_RDATASET_FIXED
	mcurrent += (4 * mcount);
#endif
	for (i = 0; i < mcount; i++) {
		unsigned char *mrdatabegin = mcurrent;
#if DNS_RDATASET_FIXED
		order = mcurrent[2] * 256 + mcurrent[3];
		INSIST(order < mcount);
#endif
		rdata_from_slab(&mcurrent, rdclass, type, &mrdata);
		scurrent = sstart;
		for (count = 0; count < scount; count++) {
			dns_rdata_reset(&srdata);
			rdata_from_slab(&scurrent, rdclass, type, &srdata);
			if (dns_rdata_compare(&mrdata, &srdata) == 0)
				break;
		}
		if (count == scount) {
			/*
			 * This rdata isn't in the sslab, and thus should be
			 * copied to the tslab.
			 */
			unsigned int length = mcurrent - mrdatabegin;
#if DNS_RDATASET_FIXED
			offsettable[order] = tcurrent - offsetbase;
#endif
			memcpy(tcurrent, mrdatabegin, length);
			tcurrent += length;
		}
		dns_rdata_reset(&mrdata);
	}

#if DNS_RDATASET_FIXED
	fillin_offsets(offsetbase, offsettable, mcount);

	isc_mem_put(mctx, offsettable, mcount * sizeof(unsigned int));
#endif

	INSIST(tcurrent == tstart + tlength);

	*tslabp = tstart;

	return (ISC_R_SUCCESS);
}

isc_boolean_t
dns_rdataslab_equal(unsigned char *slab1, unsigned char *slab2,
		    unsigned int reservelen)
{
	unsigned char *current1, *current2;
	unsigned int count1, count2;
	unsigned int length1, length2;

	current1 = slab1 + reservelen;
	count1 = *current1++ * 256;
	count1 += *current1++;

	current2 = slab2 + reservelen;
	count2 = *current2++ * 256;
	count2 += *current2++;

	if (count1 != count2)
		return (ISC_FALSE);

#if DNS_RDATASET_FIXED
	current1 += (4 * count1);
	current2 += (4 * count2);
#endif

	while (count1 > 0) {
		length1 = *current1++ * 256;
		length1 += *current1++;

		length2 = *current2++ * 256;
		length2 += *current2++;

#if DNS_RDATASET_FIXED
		current1 += 2;
		current2 += 2;
#endif

		if (length1 != length2 ||
		    memcmp(current1, current2, length1) != 0)
			return (ISC_FALSE);

		current1 += length1;
		current2 += length1;

		count1--;
	}
	return (ISC_TRUE);
}

isc_boolean_t
dns_rdataslab_equalx(unsigned char *slab1, unsigned char *slab2,
		     unsigned int reservelen, dns_rdataclass_t rdclass,
		     dns_rdatatype_t type)
{
	unsigned char *current1, *current2;
	unsigned int count1, count2;
	dns_rdata_t rdata1 = DNS_RDATA_INIT;
	dns_rdata_t rdata2 = DNS_RDATA_INIT;

	current1 = slab1 + reservelen;
	count1 = *current1++ * 256;
	count1 += *current1++;

	current2 = slab2 + reservelen;
	count2 = *current2++ * 256;
	count2 += *current2++;

	if (count1 != count2)
		return (ISC_FALSE);

#if DNS_RDATASET_FIXED
	current1 += (4 * count1);
	current2 += (4 * count2);
#endif

	while (count1-- > 0) {
		rdata_from_slab(&current1, rdclass, type, &rdata1);
		rdata_from_slab(&current2, rdclass, type, &rdata2);
		if (dns_rdata_compare(&rdata1, &rdata2) != 0)
			return (ISC_FALSE);
		dns_rdata_reset(&rdata1);
		dns_rdata_reset(&rdata2);
	}
	return (ISC_TRUE);
}
