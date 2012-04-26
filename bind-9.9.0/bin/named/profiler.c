/*
 * File: profiler.c
 *
 * Author: zakkak@csd.uoc.gr
 */

typedef struct a_node {
  isc_sockaddr_t sockaddr;      // worker's sockaddr
  uint8_t cpu_load, io_load, net_load;  // Load percentages
} a_node_t;

typedef struct ht_node_v {
  dns_adbnamehooklist_t orig_addresses;
  a_node_t *addr_stats[LWRES_MAX_ADDRS];
} ht_node_v_t;

typedef struct ht_node {
  char *key;
  ht_node_v value;
} ht_node_t;

hashtable ht_g;                 // mapping names to ht_nodes

void profiler_init()
{
  isc_result_t result;
  dns_adb_t *adb;
  unsigned int options;
  dns_adbnamehook_t *namehook;
  dns_adbentry_t *entry;
  dns_view_t *view;

  // For the rbt iterator
  dns_name_t foundname, *origin;
  dns_rbtnodechain_t chain;
  dns_fixedname_t fixedorigin;

  dns_rbtnodechain_init(&chain, mctx);

  dns_name_init(&foundname, NULL);
  dns_fixedname_init(&fixedorigin);
  origin = dns_fixedname_name(&fixedorigin);
  //////////////////////////////////////////////////////////////////////////////


  options = 0;
  options |= DNS_ADBFIND_WANTEVENT;
  options |= DNS_ADBFIND_RETURNLAME;
  // ZAKKAK not supporting IPv6
  options |= DNS_ADBFIND_INET;

  // Go through all views and initialize the hashtable
  for (view = ISC_LIST_HEAD(ns_g_server->viewlist); view != NULL; view = ISC_LIST_NEXT(view, link)) {
    adb = view->adb;

    // wait till load is complete
    while (view->zonetable->loads_pending);

    // lock the zonetable
    RWLOCK(&zonetable->rwlock, isc_rwlocktype_read);
    result = dns_rbtnodechain_first(&chain, view->zonetable->table, &foundname, origin);

    if (result != ISC_R_SUCCESS && result != DNS_R_NEWORIGIN)
      printf("start not found!\n");
    else {
      // now go through the zonetable and for each zone add a node to ht_g
      for (;;) {
        if (result == ISC_R_SUCCESS || result == DNS_R_NEWORIGIN) {

          namehook = ISC_LIST_HEAD(foundname->v4);
          // iter through addresses
          while (namehook != NULL) {
            entry = namehook->entry;
            bucket = entry->lock_bucket;
            LOCK(&adb->entrylocks[bucket]);

            entry->sockaddr;

            UNLOCK(&adb->entrylocks[bucket]);
            bucket = DNS_ADB_INVALIDBUCKET;
            namehook = ISC_LIST_NEXT(namehook, plink);
          }

        } else {
          if (result != ISC_R_NOMORE)
            printf("UNEXEPCTED ITERATION ERROR: %s", dns_result_totext(result));
          break;
        }

        result = dns_rbtnodechain_next(&chain, &foundname, origin);
      }
    }

    RWUNLOCK(&zonetable->rwlock, isc_rwlocktype_read);
  }

#if 0
  dns_zone_t **zonep dns_fixedname_t name;
  dns_adbname_t *adbname;
  isc_buffer_t buf;
  dns_rdataclass_t rdclass;

  isc_buffer_init(&buf, zonename, strlen(zonename));
  isc_buffer_add(&buf, strlen(zonename));
  dns_fixedname_init(&name);
  result = dns_name_fromtext(dns_fixedname_name(&name), &buf, dns_rootname, 0, NULL);
  if (result != ISC_R_SUCCESS)
    goto fail1;

  // what's the class?
  rdclass = dns_rdataclass_in;

  result = dns_viewlist_findzone(&ns_g_server->viewlist, dns_fixedname_name(&name), ISC_TRUE, rdclass, zonep);

  /* Partial match? */
  if (result != ISC_R_SUCCESS && *zonep != NULL)
    dns_zone_detach(zonep);
  if (result == DNS_R_PARTIALMATCH)
    result = ISC_R_NOTFOUND;

  if (result == ISC_R_SUCCESS) {
    adb = zonep->view->adb;

    /*
     * Try to see if we know anything about this name at all.
     */
    bucket = DNS_ADB_INVALIDBUCKET;
    adbname = find_name_and_lock(adb, name, options, &bucket);
    INSIST(bucket != DNS_ADB_INVALIDBUCKET);
    if (adb->name_sd[bucket]) {
      DP(DEF_LEVEL, "named_profiler: returning ISC_R_SHUTTINGDOWN");
      result = ISC_R_SHUTTINGDOWN;
      goto out;
    }

    /*
     * Nothing found.  Allocate a new adbname structure for this name.
     */
    if (adbname == NULL) {
      // ZAKKAK it would be nice to never come here
      /*
       * See if there is any stale name at the end of list, and purge
       * it if so.
       */
      check_stale_name(adb, bucket, now);

      adbname = new_adbname(adb, name);
      if (adbname == NULL) {
        result = ISC_R_NOMEMORY;
        goto out;
      }
      link_name(adb, bucket, adbname);
    } else {
      /* Move this name forward in the LRU list */
      ISC_LIST_UNLINK(adb->names[bucket], adbname, plink);
      ISC_LIST_PREPEND(adb->names[bucket], adbname, plink);
    }
    adbname->last_used = now;

    // iter through addresses
    namehook = ISC_LIST_HEAD(name->v4);
    while (namehook != NULL) {
      entry = namehook->entry;
      bucket = entry->lock_bucket;
      LOCK(&adb->entrylocks[bucket]);

      entry->sockaddr;

      UNLOCK(&adb->entrylocks[bucket]);
      bucket = DNS_ADB_INVALIDBUCKET;
      namehook = ISC_LIST_NEXT(namehook, plink);
    }
  }

fail1:
  return (result);
#endif

}


// LOCK list before sorting
void profiler_sort_addrs()
{

}
