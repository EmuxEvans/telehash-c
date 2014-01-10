#ifndef switch_h
#define switch_h

#include "hn.h"
#include "hnt.h"
#include "packet.h"
#include "xht.h"
#include "chan.h"

typedef struct switch_struct
{
  hn_t id;
  hnt_t seeds, *buckets;
  packet_t out, last; // packets waiting to be delivered
  chan_t in; // channels waiting to be processed
  int cap;
  xht_t index;
} *switch_t;

switch_t switch_new(hn_t id);
void switch_free(switch_t s);

// add hashname as a seed, will automatically trigger a query to it
void switch_seed(switch_t s, hn_t hn);

// get a packet to be sent, NULL if none
packet_t switch_sending(switch_t s);

// get a channel that has packets to be processed, NULL if none
chan_t switch_pop(switch_t s);

// add a packet to the send queue
void switch_send(switch_t s, packet_t p);

// adjust/set the cap of how many lines to maintain, defaults to 256
void switch_cap(switch_t s, int cap);


#endif