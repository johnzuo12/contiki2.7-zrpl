/**
 * \addtogroup uip6
 * @{
 */

/*
 * Copyright (c) 2013, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 */

/**
 * \file
 *         IPv6 Neighbor cache (link-layer/IPv6 address mapping)
 * \author Mathilde Durvy <mdurvy@cisco.com>
 * \author Julien Abeille <jabeille@cisco.com>
 * \author Simon Duquennoy <simonduq@sics.se>
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "lib/list.h"
#include "net/rime/rimeaddr.h"
#include "net/packetbuf.h"
#include "net/uip-ds6-nbr.h"
#include "net/rpl/rpl.h"

#define DEBUG 1//DEBUG_NONE 
#include "net/uip-debug.h"

#ifdef UIP_CONF_DS6_NEIGHBOR_STATE_CHANGED
#define NEIGHBOR_STATE_CHANGED(n) UIP_CONF_DS6_NEIGHBOR_STATE_CHANGED(n)
void NEIGHBOR_STATE_CHANGED(uip_ds6_nbr_t *n);
#else
#define NEIGHBOR_STATE_CHANGED(n)
#endif /* UIP_DS6_CONF_NEIGHBOR_STATE_CHANGED */

#ifdef UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK
#define LINK_NEIGHBOR_CALLBACK(addr, status, numtx) UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK(addr, status, numtx)
void LINK_NEIGHBOR_CALLBACK(const rimeaddr_t *addr, int status, int numtx);
#else
#define LINK_NEIGHBOR_CALLBACK(addr, status, numtx)
#endif /* UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK */

NBR_TABLE_GLOBAL(uip_ds6_nbr_t, ds6_neighbors);
#ifdef ROUTER
NBR_TABLE_GLOBAL(uip_ds6_nbr_t, outsubnet_table);
NBR_TABLE_GLOBAL(uip_ds6_nbr_t, insubnet_table);
NBR_TABLE_GLOBAL(uip_ds6_nbr_t, leaf_table);
#endif
#ifdef LEAF
NBR_TABLE_GLOBAL(uip_ds6_nbr_t, agent_table);                // agency router table for leaf. At present only one agency is selected
#endif 

/*---------------------------------------------------------------------------*/
void
uip_ds6_neighbors_init(void)
{
  nbr_table_register(ds6_neighbors, (nbr_table_callback *)uip_ds6_nbr_rm);
#ifdef ROUTER
  nbr_table_register(outsubnet_table, (nbr_table_callback *)uip_ds6_nbr_rm);
  nbr_table_register(insubnet_table, (nbr_table_callback *)uip_ds6_nbr_rm);
  nbr_table_register(leaf_table, (nbr_table_callback *)uip_ds6_nbr_rm);
#endif   /*ROUTER*/
#ifdef LEAF
    nbr_table_register(agent_table, (nbr_table_callback *)uip_ds6_nbr_rm);
#endif   /*LEAF*/
  
}
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_nbr_add(nbr_table_t *nbr_table, uip_ipaddr_t *ipaddr, uip_lladdr_t *lladdr,
                uint8_t isrouter, uint8_t state)
{
  uip_ds6_nbr_t *nbr = nbr_table_add_lladdr(nbr_table, (rimeaddr_t*)lladdr);
  if(nbr) {
    uip_ipaddr_copy(&nbr->ipaddr, ipaddr);
    nbr->isrouter = isrouter;
    nbr->state = state;
  #if UIP_CONF_IPV6_QUEUE_PKT
    uip_packetqueue_new(&nbr->packethandle);
  #endif /* UIP_CONF_IPV6_QUEUE_PKT */
    /* timers are set separately, for now we put them in expired state */
    stimer_set(&nbr->reachable, 0);
    stimer_set(&nbr->sendns, 0);
    nbr->nscount = 0;
	/*
    PRINTF("Adding neighbor with ip addr ");
    PRINT6ADDR(ipaddr);
    PRINTF(" link addr ");
    PRINTLLADDR(lladdr);
    PRINTF(" state %u\n", state);
	*/
    NEIGHBOR_STATE_CHANGED(nbr);
    return nbr;
  } else {
    PRINTF("uip_ds6_nbr_add drop ip addr ");
    PRINT6ADDR(ipaddr);
    PRINTF(" link addr (%p) ", lladdr);
    PRINTLLADDR(lladdr);
    PRINTF(" state %u\n", state);
    return NULL;
  }
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_nbr_rm(nbr_table_t *nbr_table, uip_ds6_nbr_t *nbr)
{
  if(nbr != NULL) {
#if UIP_CONF_IPV6_QUEUE_PKT
    uip_packetqueue_free(&nbr->packethandle);
#endif /* UIP_CONF_IPV6_QUEUE_PKT */
    NEIGHBOR_STATE_CHANGED(nbr);
    nbr_table_remove(nbr_table, nbr);
  }
  return;
}

/*---------------------------------------------------------------------------*/
uip_ipaddr_t *
uip_ds6_nbr_get_ipaddr(uip_ds6_nbr_t *nbr)
{
  return (nbr != NULL) ? &nbr->ipaddr : NULL;
}

/*---------------------------------------------------------------------------*/
uip_lladdr_t *
uip_ds6_nbr_get_ll(nbr_table_t *nbr_table, uip_ds6_nbr_t *nbr)
{
  return (uip_lladdr_t *)nbr_table_get_lladdr(nbr_table, nbr);
}
/*---------------------------------------------------------------------------*/
int
uip_ds6_nbr_num(nbr_table_t *nbr_table)
{
  uip_ds6_nbr_t *nbr;
  int num;

  num = 0;
  for(nbr = nbr_table_head(nbr_table);
      nbr != NULL;
      nbr = nbr_table_next(nbr_table, nbr)) {
    num++;
  }
  return num;
}
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_nbr_lookup(nbr_table_t *nbr_table, uip_ipaddr_t *ipaddr)
{
  uip_ds6_nbr_t *nbr = nbr_table_head(nbr_table);
  if(ipaddr != NULL) {
    while(nbr != NULL) {
      if(uip_ipaddr_cmp(&nbr->ipaddr, ipaddr)) {
        return nbr;
      }
      nbr = nbr_table_next(nbr_table, nbr);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_nbr_ll_lookup(nbr_table_t *nbr_table, uip_lladdr_t *lladdr)
{
  return nbr_table_get_from_lladdr(nbr_table, (rimeaddr_t*)lladdr);
}

/*---------------------------------------------------------------------------*/
uip_ipaddr_t *
uip_ds6_nbr_ipaddr_from_lladdr(nbr_table_t *nbr_table, uip_lladdr_t *lladdr)
{
  uip_ds6_nbr_t *nbr = uip_ds6_nbr_ll_lookup(nbr_table, lladdr);
  return nbr ? &nbr->ipaddr : NULL;
}

/*---------------------------------------------------------------------------*/
uip_lladdr_t *
uip_ds6_nbr_lladdr_from_ipaddr(nbr_table_t *nbr_table, uip_ipaddr_t *ipaddr)
{
  uip_ds6_nbr_t *nbr = uip_ds6_nbr_lookup(nbr_table, ipaddr);
  return nbr ? uip_ds6_nbr_get_ll(nbr_table, nbr) : NULL;
}
/*---------------------------------------------------------------------------*/
void
uip_ds6_link_neighbor_callback(int status, int numtx)
{
  const rimeaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(rimeaddr_cmp(dest, &rimeaddr_null)) {
    return;
  }

  LINK_NEIGHBOR_CALLBACK(dest, status, numtx);

#if UIP_DS6_LL_NUD
  if(status == MAC_TX_OK) {
    uip_ds6_nbr_t *nbr;
    nbr = uip_ds6_nbr_ll_lookup((uip_lladdr_t *)dest);
    if(nbr != NULL &&
        (nbr->state == NBR_STALE || nbr->state == NBR_DELAY ||
         nbr->state == NBR_PROBE)) {
      nbr->state = NBR_REACHABLE;
      stimer_set(&nbr->reachable, UIP_ND6_REACHABLE_TIME / 1000);
      PRINTF("uip-ds6-neighbor : received a link layer ACK : ");
      PRINTLLADDR((uip_lladdr_t *)dest);
      PRINTF(" is reachable.\n");
    }
  }
#endif /* UIP_DS6_LL_NUD */

}
/*---------------------------------------------------------------------------*/
void
uip_ds6_neighbor_periodic(nbr_table_t *nbr_table)
{
  /* Periodic processing on neighbors */
  uip_ds6_nbr_t *nbr = nbr_table_head(nbr_table);
  while(nbr != NULL) {
    switch(nbr->state) {
    case NBR_REACHABLE:
      if(stimer_expired(&nbr->reachable)) {
        PRINTF("REACHABLE: moving to STALE (");
        PRINT6ADDR(&nbr->ipaddr);
        PRINTF(")\n");
        nbr->state = NBR_STALE;
      }
      break;
#if UIP_ND6_SEND_NA
    case NBR_INCOMPLETE:
      if(nbr->nscount >= UIP_ND6_MAX_MULTICAST_SOLICIT) {
        uip_ds6_nbr_rm(nbr_table,nbr);
      } else if(stimer_expired(&nbr->sendns) && (uip_len == 0)) {
        nbr->nscount++;
        PRINTF("NBR_INCOMPLETE: NS %u\n", nbr->nscount);
        uip_nd6_ns_output(NULL, NULL, &nbr->ipaddr);
        stimer_set(&nbr->sendns, uip_ds6_if.retrans_timer / 1000);
      }
      break;
    case NBR_DELAY:
      if(stimer_expired(&nbr->reachable)) {
        nbr->state = NBR_PROBE;
        nbr->nscount = 0;
        PRINTF("DELAY: moving to PROBE\n");
        stimer_set(&nbr->sendns, 0);
      }
      break;
    case NBR_PROBE:
      if(nbr->nscount >= UIP_ND6_MAX_UNICAST_SOLICIT) {
        uip_ds6_defrt_t *locdefrt;
        PRINTF("PROBE END\n");
        if((locdefrt = uip_ds6_defrt_lookup(&nbr->ipaddr)) != NULL) {
          if (!locdefrt->isinfinite) {
            uip_ds6_defrt_rm(locdefrt);
          }
        }
        uip_ds6_nbr_rm(nbr_table,nbr);
      } else if(stimer_expired(&nbr->sendns) && (uip_len == 0)) {
        nbr->nscount++;
        PRINTF("PROBE: NS %u\n", nbr->nscount);
        uip_nd6_ns_output(NULL, &nbr->ipaddr, &nbr->ipaddr);
        stimer_set(&nbr->sendns, uip_ds6_if.retrans_timer / 1000);
      }
      break;
#endif /* UIP_ND6_SEND_NA */
    default:
      break;
    }
    nbr = nbr_table_next(nbr_table, nbr);
  }
}

/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_get_least_lifetime_neighbor(nbr_table_t *nbr_table)
{
  uip_ds6_nbr_t *nbr = nbr_table_head(nbr_table);
  uip_ds6_nbr_t *nbr_expiring = NULL;
  while(nbr != NULL) {
    if(nbr_expiring != NULL) {
      clock_time_t curr = stimer_remaining(&nbr->reachable);
      if(curr < stimer_remaining(&nbr->reachable)) {
        nbr_expiring = nbr;
      }
    } else {
      nbr_expiring = nbr;
    }
    nbr = nbr_table_next(nbr_table, nbr);
  }
  return nbr_expiring;
}
/*---------------------------------------------------------------------------*/
#ifdef ROUTER
int
add_to_subnet_route_table(uip_ipaddr_t *ipaddr,uip_lladdr_t *lladdr)
{
	uip_ds6_nbr_t *nbr ;
	if (ipaddr->u16[3] ==  my_info->my_prefix) {                         // router in my subnet
		if((nbr = uip_ds6_nbr_lookup(insubnet_table, ipaddr)) == NULL)       
		if((nbr = uip_ds6_nbr_add(insubnet_table,ipaddr, lladdr,
								  0, NBR_REACHABLE)) != NULL){
			PRINTF("I add router ");
			PRINT6ADDR(ipaddr);
			PRINTF(" num %d to my insubnet table\n",ipaddr->u8[15]);
			return 1;
	        }    
	}                
	else {                                                                                           // router out of my subnet
	nbr = nbr_table_head(outsubnet_table);
		while(nbr != NULL) {
			if(nbr->ipaddr.u16[3]==ipaddr->u16[3]) 
				return 0;
			nbr = nbr_table_next(outsubnet_table, nbr);
		}
	if((nbr = uip_ds6_nbr_add(outsubnet_table,ipaddr, lladdr,
							  0, NBR_REACHABLE)) != NULL)
							  {
							  PRINTF("I add router ");
			                  PRINT6ADDR(ipaddr);
			                  PRINTF(" num %d to my outsubnet table\n",ipaddr->u8[15]);
		                      return 1;
		}
	}
	    return 0;
}
/*---------------------------------------------------------------------------*/
int
add_to_leaf_table(uip_ipaddr_t *ipaddr, uip_lladdr_t *lladdr)
{
	uip_ds6_nbr_t *nbr = nbr_table_head(leaf_table);
	if((nbr = uip_ds6_nbr_add(leaf_table,ipaddr, lladdr,
							  0, NBR_REACHABLE)) != NULL)
	{
		PRINTF("I add leaf");
		PRINT6ADDR(ipaddr);
		PRINTF(" num %d to my leaf table\n",ipaddr->u8[15]);
		return 1;
	}
	return 0;
}
/*---------------------------------------------------------------------------*/
/*search for next hop*/
uip_ipaddr_t *
next_route(uip_ipaddr_t *ipaddr)
{
	if (ipaddr->u16[3] == my_info->my_prefix){
		if (ipaddr->u16[2] == my_info->my_address.u16[7]) {  // this is my leaf
			ipaddr->u16[2] = 0;                                                       
			return ipaddr;
		}
		else {              // this is for leaf of other router
			uip_ds6_nbr_t *nbr = nbr_table_head(insubnet_table);
			while(nbr != NULL) {
				if (nbr->ipaddr.u16[7] == ipaddr->u16[2]) 
					return &nbr->ipaddr;
				nbr = nbr_table_next(outsubnet_table, nbr);
			}		
			if (my_info->my_goal == RPL_SUPER_ROUTER)
				return NULL;
			else 
			return &super_router_addr;        // do not find destination router, send it to super router
		}
	}
	else {              // for destination out of subnet
		uip_ds6_nbr_t *nbr = nbr_table_head(outsubnet_table);
		uip_ipaddr_t *best_dest = NULL;
		uint16_t best_distance = 0;
		uint16_t prefix_distance = 0;
			while(nbr != NULL) {
				prefix_distance= abs(ipaddr->u8[6]-nbr->ipaddr.u8[6]) + abs(ipaddr->u8[7]-nbr->ipaddr.u8[7]);
				if (prefix_distance > best_distance) {  // find a farther subnet
					best_distance = prefix_distance;
				    uip_ipaddr_copy(best_dest, &nbr->ipaddr);
				}
				nbr = nbr_table_next(outsubnet_table, nbr);
			}				
		return best_dest;
	}
}
#endif /*ROUTER*/
/*---------------------------------------------------------------------------*/
#ifdef LEAF
int add_to_agent_table(uip_ipaddr_t *ipaddr, uip_lladdr_t *lladdr)
{
	uip_ds6_nbr_t *nbr = nbr_table_head(agent_table);
	if((nbr = uip_ds6_nbr_add(agent_table,ipaddr, lladdr,
							  0, NBR_REACHABLE)) != NULL)
		return 1;
	return 0;
}
/*---------------------------------------------------------------------------*/
uip_ipaddr_t * 
next_route(uip_ipaddr_t * ipaddr)
{
	uip_ds6_nbr_t *nbr = nbr_table_head(agent_table);
	if (nbr == NULL) 
		rpl_reset_dis_periodic_timer();  // no agent available, try to link one firstly    
	else return &nbr->ipaddr;
	return NULL;
}
#endif /*LEAF*/
