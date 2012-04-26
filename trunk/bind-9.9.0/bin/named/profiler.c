/*
 * File: profiler.c
 *
 * Author: zakkak@csd.uoc.gr
 */

// UPDATE_INTERVAL in seconds
// TODO set TTL the same
#define UPDATE_INTERVAL 60

// assert( (CPU+IO+NET)==1.0f );
#define CPU 0.5f
#define IO  0.25f
#define NET  0.25f
#define CALC_LOAD(x) (x->cpu_load*CPU + x->io_load*IO + x->net_load*NET)

typedef struct a_node {
//   isc_sockaddr_t sa;      // worker's sockaddr
  dns_adbnamehook_t *nh;
  uint8_t cpu_load, io_load, net_load;  // Load percentages
} a_node_t;

// typedef struct ht_node_v {
//   a_node_t *addr_stats[LWRES_MAX_ADDRS];
//   int naddrs;
// } ht_node_v_t;
// 
// typedef struct ht_node {
//   dns_name_t key;
//   ht_node_v value;
// } ht_node_t;
// 
// ht_hashtable_t ht_g;                 // mapping names to ht_nodes

typedef struct node {
  dns_name_t key;
  a_node_t *addr_stats[LWRES_MAX_ADDRS];
  int naddrs;
  struct node *next;
} node_t;

// typedef struct list {
//   node_t* head;
// } list_t;

node_t *list_g;

void profiler_update_addrs();

void profiler_init()
{
  int bucket;
  isc_result_t result;
  dns_view_t *view;

  // For our book keeping
  node_t *value;
  //////////////////////////////////////////////////////////////////////////////

  // For the rbt iterator
  dns_name_t foundname, *origin;
  dns_rbtnodechain_t chain;
  dns_fixedname_t fixedorigin;

  dns_rbtnodechain_init(&chain, mctx);

  dns_name_init(&foundname, NULL);
  dns_fixedname_init(&fixedorigin);
  origin = dns_fixedname_name(&fixedorigin);
  //////////////////////////////////////////////////////////////////////////////

  dns_adb_t *adb;
  dns_adbnamehook_t *namehook;
  dns_adbentry_t *entry;

  list_g = NULL;

  assert((CPU + IO + NET) == 1.0f);

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

          value = (node_t *) malloc(sizeof(node_t));
          dns_name_clone(foundname, value->key);
          value->naddrs = 0;
          value->next = list_g;
          list_g = value;

          namehook = ISC_LIST_HEAD(foundname->v4);
          // iter through addresses
          while (namehook != NULL) {
            entry = namehook->entry;
            bucket = entry->lock_bucket;
            LOCK(&adb->entrylocks[bucket]);

            value->addr_stats[value->naddrs] = (a_node_t *) malloc(sizeof(a_node_t));
            memset(value->addr_stats[value->naddrs], 0, (sizeof(a_node_t)));
//             memcpy(value->addr_stats[value->naddrs++]->sa, entry->sockaddr, sizeof(isc_sockaddr_t);
            value->addr_stats[value->naddrs++]->nh = namehook;

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

  profiler_update_addrs();
}

void profiler_poll_workers(node_t * cur)
{
  int i;
  a_node_t *tmp;

  for (i = 0; i < cur->naddrs; ++i) {
    tmp = cur->addr_stats[i];

    //TODO here do the communication and store the results
    // we can open and close the connection for each zone or keep it live for
    // the whole session
//     tmp->cpu_load = ;
//     tmp->io_load = ;
//     tmp->net_load = ;
  }
}

int cmp(const void *v_a, const void *v_b)
{
  a_node_t *a, *b;
  int s_a, s_b;

  a = (a_node_t *) v_a;
  b = (a_node_t *) v_b;

  s_a = CALC_LOAD(a);
  s_b = CALC_LOAD(b);

  return s_a - s_b;
}

void profiler_update_addrs()
{
  node_t *current = list_g;
  int i, bucket;
  dns_adbnamehook_t *tmp;
  dns_adbnamehooklist_t *list;

  while (1) {
    sleep(UPDATE_INTERVAL);

    while (current) {

      profiler_poll_workers(current);
      qsort(current->addr_stats, current->naddrs, sizeof(a_node_t), cmp);


      // LOCK list before sorting
      bucket = current->key->lock_bucket;
      LOCK(&current->key->adb->namelocks[bucket]);
      list = &(current->key->v4);
      ISC_LIST_INIT(*list);
      for (i = 0; i < cur->naddrs; ++i) {
        tmp = current->addr_stats[i]->nh;
        ISC_LIST_APPEND(*list, tmp, plink);
      }
      UNLOCK(&current->key->adb->namelocks[bucket]);

      current = list_g->next;
    }
  }
}
