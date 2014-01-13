#include "hn.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "packet.h"
#include "crypt.h"
#include "util.h"
#include "j0g.h"
#include "js0n.h"
#include "chan.h"

void hn_free(hn_t hn)
{
  if(hn->chans) xht_free(hn->chans);
  if(hn->c) crypt_free(hn->c);
  free(hn->paths);
  free(hn);
}

hn_t hn_get(xht_t index, unsigned char *bin)
{
  hn_t hn;
  
  hn = xht_get(index, (const char*)bin);
  if(hn) return hn;

  // init new hashname container
  hn = malloc(sizeof (struct hn_struct));
  bzero(hn,sizeof (struct hn_struct));
  memcpy(hn->hashname, bin, 32);
  xht_set(index, (const char*)hn->hashname, (void*)hn);
  hn->paths = malloc(sizeof (path_t));
  hn->paths[0] = NULL;
  util_hex(hn->hashname,32,(unsigned char*)hn->hexname);
  return hn;
}

hn_t hn_gethex(xht_t index, char *hex)
{
  return 0;
}

// derive a hn from json in a packet
hn_t hn_getjs(xht_t index, packet_t p)
{
  crypt_t c;
  unsigned char *key;
  hn_t hn = NULL;
  int i, len;
  unsigned short list[64];

  // load the crypt from the public key
  key = (unsigned char*)j0g_str("public",(char*)p->json,p->js);
  c = crypt_new(key, strlen((char*)key));
  if(!c) return NULL;
  
  // if there's a private key, try loading that too, only used in loading id.json and such
  if(j0g_val("private",(char*)p->json,p->js))
  {
    key = (unsigned char*)j0g_str("private",(char*)p->json,p->js);
    if(crypt_private(c,key,strlen((char*)key)))
    {
      crypt_free(c);
      return NULL;
    }
  }

  // get/update our hn value
  hn = hn_get(index, crypt_hashname(c));
  if(hn->c) crypt_free(hn->c);
  hn->c = c;
  
  // if any paths are stored, associte them
  i = j0g_val("paths",(char*)p->json,p->js);
  if(i)
  {
    key = p->json+p->js[i];
    len = p->js[i+1];
    js0n(key, len, list, 64);
  	for(i=0;list[i];i+=2)
  	{
      hn_path(hn, path_parse(key+list[i], list[i+1]));
  	}
    
  }
  
  return hn;
}

path_t hn_path(hn_t hn, path_t p)
{
  path_t existing = NULL;
  int i;

  if(!p) return NULL;

  // find existing matching path
  for(i=0;hn->paths[i];i++)
  {
    if(path_match(hn->paths[i], p)) existing = hn->paths[i];
  }
  if(existing)
  {
    path_free(p);
    return existing;
  }

  // add new path, i is the end of the list from above
  hn->paths = realloc(hn->paths, (i+2) * (sizeof (path_t)));
  hn->paths[i] = p;
  hn->paths[i+1] = 0; // null term

  return p;
}

// load hashname from file
hn_t hn_getfile(xht_t index, char *file)
{
  hn_t id = NULL;
  packet_t p;

  p = util_file2packet(file);
  if(!p) return NULL;
  id = hn_getjs(index, p);
  if(id) return id;
  packet_free(p);
  return NULL;
}

unsigned char hn_distance(hn_t a, hn_t b)
{
  return 0;
}

chan_t hn_chan(hn_t hn, unsigned char *id, chan_t c)
{
  if(!hn || !id) return NULL;
  if(!hn->chans) hn->chans = xht_new(17);
  if(!c) return xht_get(hn->chans,(const char*)id);
  xht_set(hn->chans,(const char*)id,c);
  return c;
}

