/*
 * File: profiler.c
 *
 * Authors:
 *  zakkak@csd.uoc.gr
 *  hassapis@csd.uoc.gr
 */


#include <pthread.h>
#include <stdlib.h>

#include <named/globals.h>
#include <named/server.h>
#include <named/profiler.h>

#include <lwres/lwres.h>

#include <isc/mutex.h>
#include <isc/util.h>
#include <isc/thread.h>

#include <dns/name.h>
#include <dns/rbt.h>
#include <dns/fixedname.h>
#include <dns/result.h>
#include <dns/adb.h>
#include <dns/db.h>
#include <dns/types.h>
#include <dns/zt.h>
#include <dns/rdataslab.h>
#include <dns/dbiterator.h>
#include <dns/rdatasetiter.h>

#include <assert.h>
#include <unistd.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/evp.h>

// UPDATE_INTERVAL in seconds
// TODOZ set TTL the same as UPDATE_INTERVAL lib/dns/rdataslab.c:705
#define UPDATE_INTERVAL 5

// assert( (CPU+IO+NET)==1.0f );
#define CPU 0.5f
#define IO  0.25f
#define NET  0.25f
#define CALC_LOAD(x) (x->cpu_load*CPU + x->io_load*IO + x->net_load*NET)

#define MYPRINT(...) fprintf(stderr, __VA_ARGS__)
#define EPRINT(...) do{ fprintf(stderr, "\033[1;31m"); MYPRINT(__VA_ARGS__); fprintf(stderr, "\033[m"); exit(1); }while(0)
          
#define DEBUG 0
#define LB_STATS 1

#if DEBUG
#define DPRINT MYPRINT
#else
#define DPRINT(...)
#endif

///////////////////////////////// RDTSC ///////////////////////////////////////
// #if LB_STATS
#ifdef __GNUC__
#define VOLATILE __volatile__
#define ASM __asm__
#else
/* we can at least hope the following works, it probably won't */
#define ASM asm
#define VOLATILE 
#endif

#define INT64 unsigned long long
#define INT32 unsigned int

typedef union
{       INT64 int64;
	        struct {INT32 lo, hi;} int32;
} tsc_counter;

#define RDTSC(cpu_c) \
	 ASM VOLATILE ("rdtsc" : "=a" ((cpu_c).int32.lo), "=d"((cpu_c).int32.hi))
#define CPUID(x) \
	 ASM VOLATILE ("cpuid" : "=a" (x) : "0" (x) : "bx", "cx", "dx" )

// #endif
///////////////////////////////////////////////////////////////////////////////

// typedef struct ht_node_v {
//   ns_profiler_a_node_t *addr_stats[LWRES_MAX_ADDRS];
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
//   dns_adbname_t *key;
  dns_rdataset_t rdataset;
  ns_profiler_a_node_t *addr_stats;
  int naddrs;
  struct node *next;
} node_t;

// typedef struct list {
//   node_t* head;
// } list_t;

node_t *list_g;

#if 0 //cond_timedwait
pthread_mutex_t fakeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fakeCond = PTHREAD_COND_INITIALIZER;
#endif

#define LB_MWSTATS 0

static inline void mywait(int timeInSec)
{
#if 1 //cond_timedwait and nanosleep
  struct timespec timeToWait;
#else
  struct timeval timeToWait;
#endif
  
#if LB_MWSTATS
  tsc_counter tsc_start, tsc_end;
  unsigned int counter=0;
  unsigned long long int yielded=0;
#endif

  struct timeval now;

  if(timeInSec<1)
    return;

  gettimeofday(&now,NULL);

  timeToWait.tv_sec = now.tv_sec + timeInSec;

#if 1 //nanosleep
  timeToWait.tv_sec = timeInSec;
  timeToWait.tv_nsec = 0;
  assert(nanosleep(&timeToWait, NULL)==0);
#elif 1 //cond_timedwait
  timeToWait.tv_nsec = now.tv_usec*1000;
  
  pthread_mutex_lock(&fakeMutex);
#if LB_MWSTATS
  RDTSC(tsc_start);
#endif
  pthread_cond_timedwait(&fakeCond, &fakeMutex, &timeToWait);
#if LB_MWSTATS
  RDTSC(tsc_end);
  fprintf(stderr, "Waited %lf ms\n", (float)(tsc_end.int64-tsc_start.int64)/3200000.0);
#endif
  pthread_mutex_unlock(&fakeMutex);
#elif 0 //busy_spin
  timeToWait.tv_usec = now.tv_usec;
  
  gettimeofday(&now,NULL);
  while( timeToWait.tv_sec > now.tv_sec || ((timeToWait.tv_sec == now.tv_sec) && timeToWait.tv_usec > now.tv_usec)) {
    gettimeofday(&now,NULL);
  }
#else
  timeToWait.tv_usec = now.tv_usec;

#if LB_MWSTATS
  RDTSC(tsc_start);
#endif
  pthread_yield();
#if LB_MWSTATS
  RDTSC(tsc_end);
  yielded+=tsc_end.int64-tsc_start.int64;
#endif
  gettimeofday(&now,NULL);
  while( timeToWait.tv_sec > now.tv_sec || ((timeToWait.tv_sec == now.tv_sec) && timeToWait.tv_usec > now.tv_usec)) {
#if LB_MWSTATS
    counter++;
    RDTSC(tsc_start);
#endif
    pthread_yield();
#if LB_MWSTATS
    RDTSC(tsc_end);
    yielded+=tsc_end.int64-tsc_start.int64;
#endif
    gettimeofday(&now,NULL);
  }
  
#if LB_MWSTATS
  fprintf(stderr, "AVG Yielded ticks=%llu times tried=%u\n", yielded/counter, counter);
#endif
#endif
}

/*** WORKER COMMUNICATION FUNCTIONS ***/

static inline void error(const char *msg)
{
  perror(msg);
  exit(0);
}

static inline void print2hex(unsigned const char *string, int size)
{
  int i;
  for (i = 0; i < size; i++)
    printf("%02x", string[i]);
  printf("\n");
}

static inline char *md5_digest(const char *input, size_t size)
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
    EPRINT("Unable to init MD5 digest\n");
    exit(1);
  }

  EVP_MD_CTX_init(&mdctx);
  EVP_DigestInit_ex(&mdctx, md, NULL);
  EVP_DigestUpdate(&mdctx, input, size);
  /* to add more data to hash, place additional calls to EVP_DigestUpdate here */
  EVP_DigestFinal_ex(&mdctx, output, &output_len);
  EVP_MD_CTX_cleanup(&mdctx);

  /* Now output contains the hash value, output_len contains length of output, which is 128 bit or 16 byte in case of MD5 */
  return (char *) output;
}

static inline int parse_response(char *response, ns_profiler_a_node_t * currnode)
{
  int i;
  
  memcpy( &currnode->io_load, response, 4);
  memcpy( &currnode->cpu_load, response+4, 4);
  memcpy( &currnode->net_load, response+8, 4);
  
  char msg_digest[16];
  memcpy(msg_digest, response+12, 16);
  response[12]='\0';
  //print2hex(msg_digest, 16);
  char *digest = md5_digest(response, 12);
  //print2hex(digest, 16);  
  for (i = 0; i < 16; i++) {
    if (msg_digest[i] != digest[i]) {
      DPRINT("Hash check failed\n");      
      return -1;
    }
  }    
  //DPRINT("\nMessage stats\n");
  //DPRINT("\t\tcpu load=%lf\n", currnode->cpu_load);
  //DPRINT("\t\tio load=%lf\n", currnode->io_load);
  //DPRINT("\t\tnet load=%lf\n", currnode->net_load);
  return 0;
}

static inline int connectToServer(int sockfd, char *ip, int port)
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
    //error("ERROR connecting");
    return 1;
  }
  return 0;
}

static inline char *sendMessage(char *orig_message, char *ip, int sockfd)
{

  int n;
  char *response = (char *) malloc(42 * sizeof(char));
  char *digest = md5_digest(orig_message, strlen(orig_message));
  char message[256];
  bzero(message, 256);
  strcpy(message, orig_message);
  strcat(message, "#");
  strcat(message, ip);
  strcat(message, "#");
  //digest contains some rubbish data at the end of string
  //strncat fixes the issue by copying only the usefull bytes to message
  strncat(message, digest, 16);
  //print2hex(digest, strlen(digest));
  //DPRINT("\tmessage size=%d\n", strlen(digest));
  n = write(sockfd, message, strlen(message));
  if (n < 0)
    error("ERROR writing to socket");
  //bzero(response, 42);
  //fflush(sockfd);
  n = recv(sockfd, response, 28, 0);
  response[28]='\0';
  if (n <= 0) {
    DPRINT("Recv failed with code:%d\n", n);    
    return NULL;
  }
  return response;
}


static inline void ns_profiler_poll_workers(node_t * cur)
{
  int i;
  int sockfd;
  char *ip, *ip2;
  char *response, *message = strdup("REQSTATS");
  int port = 2113;
  DPRINT("\tNumber of addresses to poll %d\n", cur->naddrs);
  for (i = 0; i < cur->naddrs; ++i) {

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
#if 0
    cur->addr_stats[i].cpu_load = (double) (cur->naddrs - i);
    cur->addr_stats[i].io_load = (double) (cur->naddrs - i);
    cur->addr_stats[i].net_load = (double) (cur->naddrs - i);
#else
    //FIXMEZ: inet_ntoa works fine, I cannot however configure bind correctly to get more names-ips
    ip2 = inet_ntoa(cur->addr_stats[i].in_addr);
    //ip = strdup("139.91.70.90");
    ip = strdup("192.168.1.201");
    DPRINT("\tPolling ip %s\n", ip2);
    //if(strcmp(ip, "0,0,0,0,") == 0) {
    //  DPRINT("\tSkipping localhost\n");
    //  continue;
    //}
    if (connectToServer(sockfd, ip, port)) {
      DPRINT(stderr, "Could not connect to worker %s\n", ip);
      close(sockfd);
      cur->addr_stats[i].cpu_load = 255.0f;
      cur->addr_stats[i].io_load = 255.0f;
      cur->addr_stats[i].net_load = 255.0f;
      continue;
    };
    //append ip to message
    //DPRINT("&message=%p\n", message);
    //strcat(message, "#");
    //strcat(message, ip2);
    //DPRINT("done\n");
    response = sendMessage(message, ip2, sockfd);
    if ( !response || parse_response(response, &(cur->addr_stats[i]))) {
      if (response) {
        //fprintf(stderr, "Checksum Error in worker's message\n");
        DPRINT("Checksum Error in worker's message\n");
      } else {
        //fprintf(stderr, "Socket Closed\n");
        DPRINT("Socket Closed\n");
      }
      // handle this somehow? The worker is down put it last in the list ;)
      cur->addr_stats[i].cpu_load = 255.0f;
      cur->addr_stats[i].io_load = 255.0f;
      cur->addr_stats[i].net_load = 255.0f;
    }
    if(response)
      free(response);
    //printf("%s\n",message);
    DPRINT("\t%s's Stats\n", ip2);
    DPRINT("\t\tcpu load=%lf\n", cur->addr_stats[i].cpu_load);
    DPRINT("\t\tio load=%lf\n", cur->addr_stats[i].io_load);
    DPRINT("\t\tnet load=%lf\n", cur->addr_stats[i].net_load);
    close(sockfd);
#endif
  }
  free(message);
  return;
}

/*** END OF WORKER COMMUNICATION ***/

static int cmp(const void *v_a, const void *v_b)
{
  const ns_profiler_a_node_t *a, *b;
  float s_a, s_b;

  a = (const ns_profiler_a_node_t *) v_a;
  b = (const ns_profiler_a_node_t *) v_b;

  s_a = CALC_LOAD(a);
  s_b = CALC_LOAD(b);

  return (int) (s_a - s_b);
}

static void ns_profiler_update_addrs()
{
  node_t *current;

  const char* ttl = getenv("TTL");
  if (!ttl)
	  EPRINT("You must set TTL ( bash$ TTL=0 named )");
  
  int up_interval = atoi(ttl);
#if LB_STATS
  tsc_counter tsc_start, tsc_end;
#endif
  
  while (1) {
#if LB_STATS
    RDTSC(tsc_start);
#endif
    mywait(up_interval);
#if LB_STATS
    RDTSC(tsc_end);
    fprintf(stderr, "wait ticks=%llu\n", tsc_end.int64-tsc_start.int64);
#endif
    current = list_g;

#if LB_STATS
    RDTSC(tsc_start);
#endif

    while (current) {

      if (current->naddrs > 0) {
        DPRINT("\tPolling the workers\n");
        ns_profiler_poll_workers(current);
        DPRINT("\t\tdone\n");
        DPRINT("\tSorting our local stats\n");
        qsort(current->addr_stats, current->naddrs, sizeof(ns_profiler_a_node_t), cmp);
        DPRINT("\t\tdone\n");

        //FIXMEZ LOCK rdataset before sorting (not sure if it is necessary)
        DPRINT("\tSorting the rdataset\n");
        dns_rdataslab_sort_fromrdataset(&current->rdataset, current->addr_stats);
        DPRINT("\t\tdone\n");
      }

      current = current->next;
    }

#if LB_STATS
    RDTSC(tsc_end);
    fprintf(stderr, "Polling ticks=%llu\n", tsc_end.int64-tsc_start.int64);
#endif

  }
}

static isc_threadresult_t ns_profiler_thread()
{
  isc_result_t result;
  dns_view_t *view;
  uint8_t zone_cnt = 0;
  int node_cnt = 0;

  // For our book keeping
  node_t *value;
  //////////////////////////////////////////////////////////////////////////////

  list_g = NULL;

  assert((CPU + IO + NET) == 1.0f);

  //delay us, to let named initialize
  sleep(1);

  // Go through all views and initialize the hashtable
  for (view = ISC_LIST_HEAD(ns_g_server->viewlist); view != NULL; view = ISC_LIST_NEXT(view, link)) {

    // wait till load is complete
    while (view->zonetable->loads_pending);

    // For the rbt iterator
    dns_name_t foundname, *origin;
    dns_rbtnodechain_t chain;
    dns_fixedname_t fixedorigin;
    dns_zone_t *zone;
    dns_rbtnode_t *rbt_node;

    // For the db iterator
    dns_dbiterator_t *dbiterator;
    dns_rdataset_t rdataset;
    dns_rdatasetiter_t *rdsit = NULL;
    dns_dbnode_t *db_node;
    dns_fixedname_t fixed_i;
    dns_name_t *name_i;

    // For the rdataset iterator
    dns_rdata_t rdata;
    dns_rdata_in_a_t rdata_a;

    dns_rbtnodechain_init(&chain, view->zonetable->table->mctx);

    dns_name_init(&foundname, NULL);
    dns_fixedname_init(&fixedorigin);
    origin = dns_fixedname_name(&fixedorigin);
    ////////////////////////////////////////////////////////////////////////////

    // lock the zonetable
    RWLOCK(&view->zonetable->rwlock, isc_rwlocktype_read);
    result = dns_rbtnodechain_first(&chain, view->zonetable->table, &foundname, origin);
//     result = dns_rbtnodechain_first(&chain, view->zonetable->table, NULL, NULL);

    if (result != ISC_R_SUCCESS && result != DNS_R_NEWORIGIN)
      EPRINT("start not found!\n");
    else {
      // now go through the zonetable
      for (;; result = dns_rbtnodechain_next(&chain, &foundname, origin)) {
        rbt_node = NULL;
        if (result == ISC_R_SUCCESS || result == DNS_R_NEWORIGIN) {
          result = dns_rbtnodechain_current(&chain, &foundname, origin, &rbt_node);
          if (result == ISC_R_SUCCESS) {
            zone = rbt_node->data;
            if ((zone != NULL) && (zone->type == dns_zone_master)) {
              char tmp[256];
              dns_zone_name(zone, tmp, sizeof(tmp));
              DPRINT("Current zone=\"%s\"\n", tmp);
              ++zone_cnt;
              // GO through the zone's db
              dns_fixedname_init(&fixed_i);
              name_i = dns_fixedname_name(&fixed_i);

              if (DNS_DB_VALID(zone->db)) {
                dbiterator = NULL;
                result = dns_db_createiterator(zone->db, 0, &dbiterator);
                if (result != ISC_R_SUCCESS) {
                  if (result == ISC_R_NOTIMPLEMENTED) {
                    DPRINT("Builtin probably, skipping iterator\n");
                    continue;
                  } else {
                    EPRINT("Couldn't create Iterator\n");
                  }
                }

                for (result = dns_dbiterator_first(dbiterator);
                     result == ISC_R_SUCCESS; result = dns_dbiterator_next(dbiterator)) {
                  db_node = NULL;
                  result = dns_dbiterator_current(dbiterator, &db_node, name_i);
                  if (result != ISC_R_SUCCESS)
                    continue;

                  result = dns_db_allrdatasets(zone->db, db_node, NULL, 0, &rdsit);
                  if (result != ISC_R_SUCCESS)
                    continue;

                  // Now go through the rdatasets
                  for (result = dns_rdatasetiter_first(rdsit);
                       result == ISC_R_SUCCESS; result = dns_rdatasetiter_next(rdsit)) {
                    // get the current dataset
                    dns_rdataset_init(&rdataset);
                    dns_rdatasetiter_current(rdsit, &rdataset);


                    //TODOZ find a way to skip builtin rdatasets (this is for efficiency)

										int count = dns_rdataset_count(&rdataset);
										if (count > 1) {

		                  // For each rdataset create a node in ht_g
		                  value = (node_t *) malloc(sizeof(node_t));
		                  DPRINT("Alloc value: %p\n", value);
		                  dns_rdataset_init(&value->rdataset);
		                  dns_rdataset_clone(&rdataset, &value->rdataset);
		                  value->next = list_g;
		                  value->naddrs = count;
		                  value->addr_stats = (ns_profiler_a_node_t *) malloc(value->naddrs * sizeof(ns_profiler_a_node_t));
		                  DPRINT("Alloc addr_stats: %p\n", value->addr_stats);
		                  memset(value->addr_stats, 0, (value->naddrs * sizeof(ns_profiler_a_node_t)));
		                  list_g = value;
		                  ++node_cnt;

		                  // transform the rdataset
		                  dns_rdataslab_transformrdataset(&rdataset, value->addr_stats);

		                  int i = 0;
		                  for (result = dns_rdataset_first(&rdataset);
		                       result == ISC_R_SUCCESS; result = dns_rdataset_next(&rdataset)) {

		                    dns_rdata_init(&rdata);
		                    dns_rdataset_current(&rdataset, &rdata);

		                    if (rdata.type == dns_rdatatype_a) {
		                      result = dns_rdata_tostruct(&rdata, &rdata_a, NULL);
		                      RUNTIME_CHECK(result == ISC_R_SUCCESS);

		                      // push the rdatas in the node
		                      memcpy(&value->addr_stats[i++].in_addr, &rdata_a.in_addr, sizeof(struct in_addr));
		                      //                     value->addr_stats[value->naddrs++]->s_addr = rdata_a.in_addr.s_addr;
		                    } else
		                    	value->naddrs--;
		                    
		                  }
		                  
		                }

                  }

                  dns_rdatasetiter_destroy(&rdsit);
                }

                dns_dbiterator_destroy(&dbiterator);
              } else {
                DPRINT("Non valid zone db, skipping iterator\n");
                continue;
              }
            }
          }
        } else {
          if (result != ISC_R_NOMORE)
            EPRINT("UNEXEPCTED ITERATION ERROR: %s", dns_result_totext(result));
          break;
        }
      }

      DPRINT("Found %d zones in view \"%s\"\n", zone_cnt, view->name);
    }

    RWUNLOCK(&view->zonetable->rwlock, isc_rwlocktype_read);
  }

  DPRINT("Found %d nodes\n", node_cnt);

  ns_profiler_update_addrs();

  return ((isc_threadresult_t) 0);
}

void ns_profiler_init()
{
  isc_thread_t thread;
  isc_thread_create(ns_profiler_thread, NULL, &thread);
//   pthread_t thread;
//   pthread_create(&thread, NULL, ns_profiler_thread, NULL);
}
