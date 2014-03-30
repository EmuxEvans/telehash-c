#include "ext.h"

uint32_t mmh32(const char* data, size_t len_);

// validate endpoint part of an id
int chat_eplen(char *id)
{
  int at;
  if(!id) return -1;
  for(at=0;id[at];at++)
  {
    if(id[at] == '@') return at;
    if(id[at] >= 'a' && id[at] <= 'z') continue;
    if(id[at] >= '0' && id[at] <= '9') continue;
    if(id[at] == '_') continue;
    return -1;
  }
  return 0;
}

char *chat_rhash(chat_t ct)
{
  char *buf, *str;
  int at=0, i, len;
  uint32_t hash;
  // TODO, should be a way to optimize doing the mmh on the fly
  buf = malloc(ct->roster->json_len);
  for(i=0;(str = packet_get_istr(ct->roster,i));i++)
  {
    len = strlen(str);
    memcpy(buf+at,str,len);
    at += len;
  }
  hash = mmh32(buf,at);
  free(buf);
  util_hex((unsigned char*)&hash,4,(unsigned char*)ct->rhash);
  return ct->rhash;
}

chat_t chat_get(switch_t s, char *id)
{
  chat_t ct;
  packet_t note;
  hn_t orig = NULL;
  int at;
  char buf[128];

  ct = xht_get(s->index,id);
  if(ct) return ct;

  // if there's an id, validate and optionally parse out originator
  if(id)
  {
    at = chat_eplen(id);
    if(at < 0) return NULL;
    if(at > 0)
    {
      id[at] = 0;
      orig = hn_gethex(s->index,id+(at+1));
      if(!orig) return NULL;
    }
  }

  ct = malloc(sizeof (struct chat_struct));
  memset(ct,0,sizeof (struct chat_struct));
  if(!id)
  {
    crypt_rand((unsigned char*)buf,4);
    util_hex((unsigned char*)buf,4,(unsigned char*)ct->ep);
  }else{
    memcpy(ct->ep,id,strlen(id)+1);
  }
  ct->orig = orig ? orig : s->id;
  sprintf(ct->id,"%s@%s",ct->ep,ct->orig->hexname);
  ct->s = s;
  ct->roster = packet_new();
  ct->index = xht_new(101);
  ct->seq = 1000;
  crypt_rand((unsigned char*)&(ct->seed),4);

  // an admin channel for distribution and thtp requests
  ct->base = chan_new(s, s->id, "chat", 0);
  ct->base->arg = ct;
  note = chan_note(ct->base,NULL);
  sprintf(buf,"/chat/%s/",ct->ep);
  thtp_glob(s,buf,note);
  xht_set(s->index,ct->id,ct);

  // any other hashname and we try to initialize
  if(ct->orig != s->id)
  {
    ct->state = LOADING;
    // TODO fetch roster and joins
  }else{
    ct->state = OFFLINE;
  }
  return ct;
}

chat_t chat_free(chat_t ct)
{
  if(!ct) return ct;
  xht_set(ct->s->index,ct->id,NULL);
  // TODO xht-walk ct->index and free packets
  // TODO unregister thtp
  xht_free(ct->index);
  packet_free(ct->roster);
  free(ct);
  return NULL;
}

packet_t chat_message(chat_t ct)
{
  packet_t p;
  char id[32];
  uint16_t step;
  uint32_t hash;
  unsigned long at = platform_seconds();

  if(!ct || !ct->seq) return NULL;

  p = packet_new();
  memcpy(id,ct->seed,9);
  for(step=0; step <= ct->seq; step++)
  {
    hash = mmh32(id,8);
    util_hex((unsigned char*)&hash,4,(unsigned char*)id);
  }
  sprintf(id+8,",%d",ct->seq);
  ct->seq--;
  packet_set_str(p,"id",id);
  packet_set_str(p,"type","message");
  if(ct->after) packet_set_str(p,"after",ct->after);
  if(at > 1396184861) packet_set_int(p,"at",at); // only if platform_seconds() is epoch
  return p;
}

chat_t chat_join(chat_t ct, packet_t join)
{
  packet_t p;
  if(!ct || !join) return NULL;
  packet_set_str(join,"type","join");
  if(ct->join)
  {
    p = xht_get(ct->index,ct->join);
    xht_set(ct->index,ct->join,NULL);
    packet_free(p);
  }
  ct->join = packet_get_str(join,"id");
  xht_set(ct->index,ct->join,join);
  packet_set_str(ct->roster,ct->s->id->hexname,ct->join);
  chat_rhash(ct);
  if(ct->orig == ct->s->id)
  {
    ct->state = JOINED;
    return ct;
  }
  ct->state = JOINING;
  // TODO mesh
  return ct;
}

chat_t chat_send(chat_t ct, packet_t msg)
{
  if(!ct || !msg) return NULL;
  // TODO walk roster, find connected in index and send notes
  return NULL;
}

chat_t chat_default(chat_t ct, char *val)
{
  if(!ct) return NULL;
  packet_set_str(ct->roster,"*",val);
  chat_rhash(ct);
  return ct;
}

chat_t ext_chat(chan_t c)
{
  packet_t p, note, req;
  chat_t ct = c->arg;
  char *path;
  
  // if this is the base channel, it just receives notes
  if(ct && ct->base == c)
  {
    while((note = chan_notes(c)))
    {
      req = packet_linked(note);
      printf("note req packet %.*s\n", req->json_len, req->json);
      path = packet_get_str(req,"path");
      p = packet_new();
      packet_set_int(p,"status",200);
      packet_body(p,(unsigned char*)path,strlen(path));
      packet_link(note,p);
      chan_reply(c,note);
    }
    return NULL;
  }

  if(!ct && (p = chan_pop(c)))
  {
    ct = chat_get(c->s,packet_get_str(p,"to"));
    if(!ct) return (chat_t)chan_fail(c,"500");
    printf("new chat %s from %s\n",ct->id,c->to->hexname);
    return ct;
  }

  while((p = chan_pop(c)))
  {
    printf("chat packet %.*s\n", p->json_len, p->json);
    packet_free(p);
  }
  return NULL;
}



// murmurhash stuffs

static inline uint32_t fmix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

static inline uint32_t rotl32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

uint32_t mmh32(const char* data, size_t len_)
{
    const int len = (int) len_;
    const int nblocks = len / 4;

    uint32_t h1 = 0xc062fb4a;

    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;

    //----------
    // body

    const uint32_t * blocks = (const uint32_t*) (data + nblocks * 4);

    int i;
    for(i = -nblocks; i; i++)
    {
        uint32_t k1 = blocks[i];

        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1*5+0xe6546b64;
    }

    //----------
    // tail

    const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

    uint32_t k1 = 0;

    switch(len & 3)
    {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
              k1 *= c1; k1 = rotl32(k1,15); k1 *= c2; h1 ^= k1;
    }

    //----------
    // finalization

    h1 ^= len;

    h1 = fmix(h1);

    return h1;
}
