/*
 * File: profiler.c
 *
 * Authors:
 *  zakkak@csd.uoc.gr
 *  hassapis@csd.uoc.gr
 */


#include <stdlib.h>

#include <lwres/lwres.h>

#include <isc/mutex.h>
#include <isc/util.h>>
#include <isc/thread.h>

#include <dns/name.h>
#include <dns/rbt.h>
#include <dns/fixedname.h>
#include <dns/result.h>
#include <dns/adb.h>
#include <dns/db.h>
#include <dns/types.h>

#include <named/globals.h>
#include <named/server.h>
#include <named/profiler.h>

#include <assert.h>
#include <unistd.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <dns/zt.h>

// UPDATE_INTERVAL in seconds
// TODO set TTL the same
#define UPDATE_INTERVAL 5

// assert( (CPU+IO+NET)==1.0f );
#define CPU 0.5f
#define IO  0.25f
#define NET  0.25f
#define CALC_LOAD(x) (x->cpu_load*CPU + x->io_load*IO + x->net_load*NET)

#define TEST LOCK ## 1

#if (TEST == 1)
#error "Ooops"
#endif

typedef struct a_node {
//   isc_sockaddr_t sa;      // worker's sockaddr
  dns_adbnamehook_t *nh;
  //uint8_t cpu_load, io_load, net_load;  // Load percentages
  double cpu_load, io_load, net_load;
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
  dns_adbname_t *key;
  a_node_t *addr_stats[LWRES_MAX_ADDRS];
  int naddrs;
  struct node *next;
} node_t;

// typedef struct list {
//   node_t* head;
// } list_t;

node_t *list_g;

/*** WORKER COMMUNICATION FUNCTIONS ***/

static void error(const char *msg)
{
  perror(msg);
  exit(0);
}

static void print2hex(unsigned const char *string, int size)
{
  int i;
  for (i = 0; i < size; i++)
    printf("%02x", string[i]);
  printf("\n");
}

static char *md5_digest(const char *input)
{
  EVP_MD_CTX mdctx;
  const EVP_MD *md;
  //char input[] = "REQSTATS";
  unsigned char *output = (unsigned char *) malloc(sizeof(unsigned char) * 16);
  unsigned int output_len;

  /* Initialize digests table */
  OpenSSL_add_all_digests();
  md = EVP_get_digestbyname("MD5");

  if (!md) {
    printf("Unable to init MD5 digest\n");
    exit(1);
  }

  EVP_MD_CTX_init(&mdctx);
  EVP_DigestInit_ex(&mdctx, md, NULL);
  EVP_DigestUpdate(&mdctx, input, strlen(input));
  /* to add more data to hash, place additional calls to EVP_DigestUpdate here */
  EVP_DigestFinal_ex(&mdctx, output, &output_len);
  EVP_MD_CTX_cleanup(&mdctx);

  /* Now output contains the hash value, output_len contains length of output, which is 128 bit or 16 byte in case of MD5 */
  return (char*)output;
}

static int parse_response(char *response, a_node_t * currnode)
{
  char *timestamp;
  double stats[3];
  char *message = strtok(response, "#");
  char *msg_digest = strtok(NULL, "#");
  int i;
  //printf("message=%s\n", message);
  //printf("?=%s", msg_digest);
  //print2hex(msg_digest, 16);
  char *digest = md5_digest(message);
  //print2hex(digest, 16);
  for (i = 0; i < 16; i++) {
    if (msg_digest[i] != digest[i])
      return -1;
  }
  stats[0] = atof(strtok(message, "$"));
  for (i = 1; i < 3; i++) {
    stats[i] = atof(strtok(NULL, "$"));
  }
  timestamp = strtok(NULL, "$");
  printf("io usages=%lf, cpu usage=%lf, network traffic=%lf\n", stats[0], stats[1], stats[2]);
  printf("timestamp=%s\n", timestamp);
  currnode->io_load = stats[0];
  currnode->cpu_load = stats[1];
  currnode->net_load = stats[2];
  return 0;
}

static int connectToServer(int sockfd, char *ip, int port)
{
  struct sockaddr_in serv_addr;
  struct hostent *server;

  server = gethostbyname(ip);
  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host\n");
    exit(0);
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_port = htons(port);

  if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    error("ERROR connecting");
    return 1;
  }
  return 0;
}

static char *sendMessage(char *orig_message, int sockfd)
{

  int n;
  char *response = (char *) malloc(256 * sizeof(char));
  char *digest = md5_digest(orig_message);
  char message[256];
  bzero(message, 256);
  strcpy(message, orig_message);
  strcat(message, "#");
  strcat(message, digest);
  n = write(sockfd, message, strlen(message));

  if (n < 0)
    error("ERROR writing to socket");

  bzero(response, 256);
  n = read(sockfd, response, 255);
  if (n < 0)
    error("ERROR reading from socket");

  return response;
}


static void ns_profiler_poll_workers(node_t * cur)
{
  int i;
  a_node_t *tmp;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  char *ip;
//   char *response, *message = strdup("REQSTATS");
  int port = 2113;

  for (i = 0; i < cur->naddrs; ++i) {
    tmp = cur->addr_stats[i];

      tmp->cpu_load = cur->naddrs-i;
      tmp->io_load = cur->naddrs-i;
      tmp->net_load = cur->naddrs-i;
//     ip = inet_ntoa(tmp->nh->entry->sockaddr.type.sin.sin_addr); //here i try to get the ip, not sure that's fine
//     if (connectToServer(sockfd, ip, port)) {
//       fprintf(stderr, "Could not connect to worker %s\n", ip);
//       close(sockfd);
//       tmp->cpu_load = 255;
//       tmp->io_load = 255;
//       tmp->net_load = 255;
//       return;
//     };
//     response = sendMessage(message, sockfd);
//     if (parse_response(response, tmp)) {
//       fprintf(stderr, "Checksum Error in worker's message\n");
//       // handle this somehow? The worker is down put it last in the list ;)
//       tmp->cpu_load = 255;
//       tmp->io_load = 255;
//       tmp->net_load = 255;
//     }
//     //printf("%s\n",message);
//     close(sockfd);
//     free(message);
//     free(response);
    return;
  }
}

/*** END OF WORKER COMMUNICATION ***/

static int cmp(const void *v_a, const void *v_b)
{
  const a_node_t *a, *b;
  int s_a, s_b;

  a = (const a_node_t *) v_a;
  b = (const a_node_t *) v_b;

  s_a = CALC_LOAD(a);
  s_b = CALC_LOAD(b);

  return s_a - s_b;
}

static void ns_profiler_update_addrs()
{
  node_t *current = list_g;
  int i, bucket;
  dns_adbnamehook_t *tmp;
  dns_adbnamehooklist_t *list;

  while (1) {
    sleep(UPDATE_INTERVAL);
    fprintf(stderr, "Here we go again!!!\n");

    while (current) {

      ns_profiler_poll_workers(current);
      qsort(current->addr_stats, current->naddrs, sizeof(a_node_t), cmp);

      // LOCK list before sorting
      bucket = current->key->lock_bucket;
      LOCK(&current->key->adb->namelocks[bucket]);
      list = &(current->key->v4);
      ISC_LIST_INIT(*list);
      for (i = 0; i < current->naddrs; ++i) {
        tmp = current->addr_stats[i]->nh;
        ISC_LIST_APPEND(*list, tmp, plink);
      }
      UNLOCK(&current->key->adb->namelocks[bucket]);

      current = list_g->next;
    }
  }
}

static isc_threadresult_t ns_profiler_thread()
{
  int bucket, bucket_name;
  isc_result_t result;
  dns_view_t *view;
  uint8_t views_cnt=0;

  // For our book keeping
  node_t *value;
  //////////////////////////////////////////////////////////////////////////////

  dns_adb_t *adb;
  dns_adbnamehook_t *namehook;
  dns_adbentry_t *entry;

  list_g = NULL;

  assert((CPU + IO + NET) == 1.0f);
  
  //delay us for 5 seconds
  sleep(5);

  // Go through all views and initialize the hashtable
  while(1){
  sleep(5);
  for (view = ISC_LIST_HEAD(ns_g_server->viewlist); view != NULL; view = ISC_LIST_NEXT(view, link)) {
    ++views_cnt;
    adb = view->adb;
    
    dns_adb_dump(adb, stderr);
    fprintf(stderr, "%u\n", views_cnt);
    
#if 0
    // Go through all names
    for (i = 0; i < adb->nnames; i++) {
      name = ISC_LIST_HEAD(adb->names[i]);
      while (name != NULL) {
        bucket = isc_sockaddr_hash(&name->sockaddr, ISC_TRUE) % n;
        name->lock_bucket = bucket;
        ISC_LIST_APPEND(newnames[bucket], name, plink);
        INSIST(adb->name_refcnt[i] > 0);
        adb->name_refcnt[i]--;
        newname_refcnt[bucket]++;
        name = ISC_LIST_HEAD(adb->names[i]);
      }
      name = ISC_LIST_HEAD(adb->deadnames[i]);
      while (name != NULL) {
        ISC_LIST_UNLINK(adb->deadnames[i], name, plink);
        bucket = isc_sockaddr_hash(&name->sockaddr, ISC_TRUE) % n;
        name->lock_bucket = bucket;
        ISC_LIST_APPEND(newdeadnames[bucket], name, plink);
        INSIST(adb->name_refcnt[i] > 0);
        adb->name_refcnt[i]--;
        newname_refcnt[bucket]++;
        name = ISC_LIST_HEAD(adb->deadnames[i]);
      }
      INSIST(adb->name_refcnt[i] == 0);
      adb->irefcnt--;
    }
    
    for (i = 0; i < adb->nentries; i++) {
      entry = ISC_LIST_HEAD(adb->entries[i]);
      while (entry != NULL) {
        ISC_LIST_UNLINK(adb->entries[i], entry, plink);
        bucket = isc_sockaddr_hash(&entry->sockaddr, ISC_TRUE) % n;
        entry->lock_bucket = bucket;
        ISC_LIST_APPEND(newentries[bucket], entry, plink);
        INSIST(adb->entry_refcnt[i] > 0);
        adb->entry_refcnt[i]--;
        newentry_refcnt[bucket]++;
        entry = ISC_LIST_HEAD(adb->entries[i]);
      }
      entry = ISC_LIST_HEAD(adb->deadentries[i]);
      while (entry != NULL) {
        ISC_LIST_UNLINK(adb->deadentries[i], entry, plink);
        bucket = isc_sockaddr_hash(&entry->sockaddr, ISC_TRUE) % n;
        entry->lock_bucket = bucket;
        ISC_LIST_APPEND(newdeadentries[bucket], entry, plink);
        INSIST(adb->entry_refcnt[i] > 0);
        adb->entry_refcnt[i]--;
        newentry_refcnt[bucket]++;
        entry = ISC_LIST_HEAD(adb->deadentries[i]);
      }
      INSIST(adb->entry_refcnt[i] == 0);
      adb->irefcnt--;
    }

    // wait till load is complete
    while (view->zonetable->loads_pending);

    // For the rbt iterator
    dns_name_t foundname, *origin;
    dns_rbtnodechain_t chain;
    dns_fixedname_t fixedorigin;

    dns_rbtnodechain_init(&chain, view->zonetable->table->mctx);

    dns_name_init(&foundname, NULL);
    dns_fixedname_init(&fixedorigin);
    origin = dns_fixedname_name(&fixedorigin);
    //////////////////////////////////////////////////////////////////////////////

    // lock the zonetable
    RWLOCK(&view->zonetable->rwlock, isc_rwlocktype_read);
    result = dns_rbtnodechain_first(&chain, view->zonetable->table, &foundname, origin);

    if (result != ISC_R_SUCCESS && result != DNS_R_NEWORIGIN)
      printf("start not found!\n");
    else {
      // now go through the zonetable and for each zone add a node to ht_g
      for (;;) {
        if (result == ISC_R_SUCCESS || result == DNS_R_NEWORIGIN) {

          value = (node_t *) malloc(sizeof(node_t));

          // find the adbname from name
          bucket_name = dns_name_fullhash(&foundname, ISC_FALSE) % adb->nnames;
          LOCK(&adb->namelocks[bucket_name]);
          value->key = ISC_LIST_HEAD(adb->names[bucket_name]);
          while (value->key != NULL) {
            if (!NAME_DEAD(value->key)) {
              if (dns_name_equal(&foundname, &value->key->name))
                break;
            }
            value->key = ISC_LIST_NEXT(value->key, plink);
          }
          
          if(value->key) {
            value->naddrs = 0;
            value->next = list_g;
            list_g = value;

            namehook = ISC_LIST_HEAD(value->key->v4);
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
  //             bucket = DNS_ADB_INVALIDBUCKET;
              namehook = ISC_LIST_NEXT(namehook, plink);
            }
          }

          UNLOCK(&adb->namelocks[bucket_name]);
        } else {
          if (result != ISC_R_NOMORE)
            printf("UNEXEPCTED ITERATION ERROR: %s", dns_result_totext(result));
          break;
        }

        result = dns_rbtnodechain_next(&chain, &foundname, origin);
      }
    }

    RWUNLOCK(&view->zonetable->rwlock, isc_rwlocktype_read);
#endif
  }
  }

//   ns_profiler_update_addrs();
  
  return ((isc_threadresult_t)0);
}

void ns_profiler_init(){
  isc_thread_t thread;
  isc_thread_create(ns_profiler_thread, NULL, &thread);
};
