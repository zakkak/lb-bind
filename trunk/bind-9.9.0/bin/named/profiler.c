/*
 * File: profiler.c
 *
 * Author: zakkak@csd.uoc.gr
 */

// For this to work we must update the masterfile (write to disk)
//   isc_buffer_t text;
//   char textarray[1024];
//   isc_buffer_init(&text, textarray, sizeof(textarray));
//ns_server_reloadcommand(ns_g_server, "reload zonename", text);

// or better update directly the database

  dns_zone_t **zonep
  dns_fixedname_t name;
  isc_result_t result;
  isc_buffer_t buf;
  dns_rdataclass_t rdclass;
  
  isc_buffer_init(&buf, zonename, strlen(zonename));
  isc_buffer_add(&buf, strlen(zonename));
  dns_fixedname_init(&name);
  result = dns_name_fromtext(dns_fixedname_name(&name),
           &buf, dns_rootname, 0, NULL);
  if (result != ISC_R_SUCCESS)
    goto fail1;

  // what's the class?
  rdclass = dns_rdataclass_in;

  result = dns_viewlist_findzone(&ns_g_server->viewlist,
                 dns_fixedname_name(&name),
                 ISC_TRUE, rdclass, zonep);

  /* Partial match? */
  if (result != ISC_R_SUCCESS && *zonep != NULL)
    dns_zone_detach(zonep);
  if (result == DNS_R_PARTIALMATCH)
    result = ISC_R_NOTFOUND;
 fail1:
  return (result);


/*****************************************************************
**  reload a zone via "rndc"
**  borrowed from contrib/zkt/nscomm.c
**  better approach: find out how rndc does it
*****************************************************************/
int reload_zone (const char *domain)
{
  char  cmdline[254+1];
  char  str[254+1];
  FILE  *fp;

//   assert (z != NULL);
//   dbg_val3 ("reload_zone %d :%s: :%s:\n", z->verbosity, domain, z->view);
  // Not supporting views
//   if ( z->view )
//     snprintf (str, sizeof (str), "\"%s\" in view \"%s\"", domain, z->view);
//   else
//     snprintf (str, sizeof (str), "\"%s\"", domain);

  lg_mesg (LG_NOTICE, "%s: reload triggered", domain);
//   verbmesg (1, z, "\tReload zone %s\n", domain);

  // Not supporting views
//   if ( z->view )
//     snprintf (cmdline, sizeof (cmdline), "rndc reload %s IN %s", domain, z->view);
//   else
    snprintf (cmdline, sizeof (cmdline), "rndc reload %s", RELOADCMD, domain);

  *str = '\0';
//   if ( z->noexec == 0 )
//   {
//     verbmesg (2, z, "\t  Run cmd \"%s\"\n", cmdline);
    if ( (fp = popen (cmdline, "r")) == NULL || fgets (str, sizeof str, fp) == NULL )
      return -1;
    pclose (fp);
//     verbmesg (2, z, "\t  rndc reload return: \"%s\"\n", str_chop (str, '\n'));
//   }

  return 0;
}