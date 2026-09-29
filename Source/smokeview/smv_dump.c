// Render dump for smokeshow's smv2png, built only with SMV_DUMP (smv_dump.h). Records the draw
// pass that produced a rendered image; files and layout: smokeshow docs/smv2png-plan.md §5.
#ifdef SMV_DUMP
#include "options.h"
#include "glew.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include GLUT_H

#include "smokeviewvars.h"
#include "smv_dump.h"

#define DUMP_NONE  0
#define DUMP_SMOKE 1
#define DUMP_SLICE 2

typedef struct {
  int mesh, frame, smokedir, nnodes;
  char file[1024];
  unsigned char alpha[256], fire[256];
  unsigned char *merge, *alpha_out, *firenode;
} dump_smoke;

typedef struct {
  int mesh, frame;
  char file[1024];
  float valmin, valmax;
} dump_slice;

static struct {
  int has_time, iglobal;
  float requested, global_time;
  int has_camera, viewport[4];
  float modelview[16], projection[16];
  dump_smoke *smoke;
  int nsmoke, capsmoke;
  dump_slice *slice;
  int nslice, capslice;
  unsigned char *tris;
  size_t ntris_bytes, captris;
  int mode;
  unsigned char color[4];
  float t;
} dump;

static void *Grow(void *p, int *cap, int need, size_t size){
  if(need <= *cap) return p;
  *cap = need > 2 * (*cap) ? need : 2 * (*cap);
  return realloc(p, (size_t)(*cap) * size);
}

static void CaptureCamera(void){
  if(dump.has_camera) return;
  glGetFloatv(GL_MODELVIEW_MATRIX, dump.modelview);
  glGetFloatv(GL_PROJECTION_MATRIX, dump.projection);
  glGetIntegerv(GL_VIEWPORT, dump.viewport);
  dump.has_camera = 1;
}

void SmvDumpPassBegin(void){
  int i;

  for(i = 0; i < dump.nsmoke; i++){
    free(dump.smoke[i].merge);
    free(dump.smoke[i].alpha_out);
    free(dump.smoke[i].firenode);
  }
  dump.nsmoke = 0;
  dump.nslice = 0;
  dump.ntris_bytes = 0;
  dump.has_camera = 0;
  dump.mode = DUMP_NONE;
}

void SmvDumpSetTime(float requested, int iglobal, float global_time){
  dump.has_time = 1;
  dump.requested = requested;
  dump.iglobal = iglobal;
  dump.global_time = global_time;
}

void SmvDumpSmokeMesh(const struct _smoke3ddata *s, const struct _meshdata *m, const unsigned char *alpha_map,
                      const unsigned char *fire_map, const unsigned char *alpha_out){
  dump_smoke *d;
  int i, n;

  CaptureCamera();
  dump.smoke = Grow(dump.smoke, &dump.capsmoke, dump.nsmoke + 1, sizeof(dump_smoke));
  d = dump.smoke + dump.nsmoke++;
  memset(d, 0, sizeof(*d));
  n = s->nchars_uncompressed;
  d->mesh = s->blocknumber;
  d->frame = s->ismoke3d_time;
  d->smokedir = m->smokedir;
  d->nnodes = n;
  strncpy(d->file, s->file, sizeof(d->file) - 1);
  memcpy(d->alpha, alpha_map, 256);
  memcpy(d->fire, fire_map, 256);
  // smokecolor_ptr's alpha byte is scratch space for the vertex colour; the merged alpha is smokealpha_ptr
  d->merge = malloc(4 * (size_t)n);
  for(i = 0; i < n; i++){
    memcpy(d->merge + 4 * i, m->smokecolor_ptr + 4 * i, 3);
    d->merge[4 * i + 3] = m->smokealpha_ptr[i];
  }
  d->alpha_out = malloc((size_t)n);
  memcpy(d->alpha_out, alpha_out, (size_t)n);
  if(m->is_firenode != NULL){
    d->firenode = malloc((size_t)n);
    memcpy(d->firenode, m->is_firenode, (size_t)n);
  }
  dump.mode = DUMP_SMOKE;
}

void SmvDumpSliceBegin(const struct _slicedata *sd, float valmin, float valmax){
  dump_slice *d;

  CaptureCamera();
  dump.slice = Grow(dump.slice, &dump.capslice, dump.nslice + 1, sizeof(dump_slice));
  d = dump.slice + dump.nslice++;
  memset(d, 0, sizeof(*d));
  d->mesh = sd->blocknumber;
  d->frame = sd->itime;
  strncpy(d->file, sd->file, sizeof(d->file) - 1);
  d->valmin = valmin;
  d->valmax = valmax;
  dump.mode = DUMP_SLICE;
}

void SmvDumpDrawEnd(void){
  dump.mode = DUMP_NONE;
}

void SmvDumpColor4ubv(const unsigned char *c){
  memcpy(dump.color, c, 4);
  glColor4ubv(c);
}

void SmvDumpTexCoord1f(float t){
  dump.t = t;
  glTexCoord1f(t);
}

// 16 bytes per vertex: x, y, z as f32, then RGBA (smoke) or t as f32 (slice)
void SmvDumpVertex3f(float x, float y, float z){
  if(dump.mode != DUMP_NONE){
    unsigned char *r;
    float xyz[3];
    int cap = (int)dump.captris;

    dump.tris = Grow(dump.tris, &cap, (int)(dump.ntris_bytes + 16), 1);
    dump.captris = (size_t)cap;
    r = dump.tris + dump.ntris_bytes;
    xyz[0] = x; xyz[1] = y; xyz[2] = z;
    memcpy(r, xyz, 12);
    if(dump.mode == DUMP_SMOKE) memcpy(r + 12, dump.color, 4);
    else memcpy(r + 12, &dump.t, 4);
    dump.ntris_bytes += 16;
  }
  glVertex3f(x, y, z);
}

static int DumpWriteFile(const char *dir, const char *name, const void *p, size_t n){
  char path[2048];
  FILE *f;
  int ok;

  snprintf(path, sizeof(path), "%s/%s", dir, name);
  f = fopen(path, "wb");
  if(f == NULL) return 0;
  ok = n == 0 || fwrite(p, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

static void JsonFloats(FILE *f, const float *v, int n){
  int i;

  fprintf(f, "[");
  for(i = 0; i < n; i++) fprintf(f, "%s%.9g", i ? ", " : "", v[i]);
  fprintf(f, "]");
}

// JSON string body: backslashes and quotes escaped, as file names on Windows need
static void JsonString(FILE *f, const char *s){
  fputc('"', f);
  for(; *s; s++){
    if(*s == '"' || *s == '\\') fputc('\\', f);
    fputc(*s, f);
  }
  fputc('"', f);
}

void SmvDumpFlush(const char *image_file){
  const char *dir = getenv("SMV_DUMP_DIR");
  char path[2048], name[256];
  FILE *f;
  int i;

  if(dir == NULL || dir[0] == 0) return;
  snprintf(path, sizeof(path), "%s/dump.json", dir);
  f = fopen(path, "w");
  if(f == NULL){
    fprintf(stderr, "*** SMV_DUMP: cannot write %s\n", path);
    return;
  }
  fprintf(f, "{\n");
  fprintf(f, "  \"writer\": \"smokeview %s\",\n", pp_GITHASH);
  fprintf(f, "  \"image\": ");
  JsonString(f, image_file);
  fprintf(f, ",\n");
  if(dump.has_time){
    fprintf(f, "  \"time_requested\": %.9g,\n", dump.requested);
    fprintf(f, "  \"time\": %.9g,\n", dump.global_time);
    fprintf(f, "  \"global_frame\": %i,\n", dump.iglobal);
  }
  fprintf(f, "  \"viewport\": [%i, %i, %i, %i],\n", dump.viewport[0], dump.viewport[1], dump.viewport[2], dump.viewport[3]);
  fprintf(f, "  \"modelview\": ");
  JsonFloats(f, dump.modelview, 16);
  fprintf(f, ",\n  \"projection\": ");
  JsonFloats(f, dump.projection, 16);
  fprintf(f, ",\n  \"smoke\": [");
  for(i = 0; i < dump.nsmoke; i++){
    const dump_smoke *d = dump.smoke + i;

    fprintf(f, "%s\n    {\"mesh\": %i, \"file\": ", i ? "," : "", d->mesh);
    JsonString(f, d->file);
    fprintf(f, ", \"frame\": %i, \"smokedir\": %i, \"nnodes\": %i, \"firenode\": %s}", d->frame, d->smokedir, d->nnodes,
            d->firenode != NULL ? "true" : "false");
  }
  fprintf(f, "%s],\n  \"slices\": [", dump.nsmoke ? "\n  " : "");
  for(i = 0; i < dump.nslice; i++){
    const dump_slice *d = dump.slice + i;

    fprintf(f, "%s\n    {\"mesh\": %i, \"file\": ", i ? "," : "", d->mesh);
    JsonString(f, d->file);
    fprintf(f, ", \"frame\": %i, \"valmin\": %.9g, \"valmax\": %.9g}", d->frame, d->valmin, d->valmax);
  }
  fprintf(f, "%s],\n  \"tris_vertices\": %i\n}\n", dump.nslice ? "\n  " : "", (int)(dump.ntris_bytes / 16));
  fclose(f);

  DumpWriteFile(dir, "rgb_slice.f32", rgb_slice, sizeof(rgb_slice));
  DumpWriteFile(dir, "smoke_cmap.f32", rgb_slicesmokecolormap_01, sizeof(rgb_slicesmokecolormap_01));
  DumpWriteFile(dir, "tris.bin", dump.tris, dump.ntris_bytes);
  // A mesh drawn more than once in the pass keeps its last draw
  for(i = 0; i < dump.nsmoke; i++){
    const dump_smoke *d = dump.smoke + i;

    snprintf(name, sizeof(name), "mesh%i.alpha.u8", d->mesh);
    DumpWriteFile(dir, name, d->alpha, 256);
    snprintf(name, sizeof(name), "mesh%i.firealpha.u8", d->mesh);
    DumpWriteFile(dir, name, d->fire, 256);
    snprintf(name, sizeof(name), "mesh%i.merge.u8", d->mesh);
    DumpWriteFile(dir, name, d->merge, 4 * (size_t)d->nnodes);
    snprintf(name, sizeof(name), "mesh%i.alpha_out.u8", d->mesh);
    DumpWriteFile(dir, name, d->alpha_out, (size_t)d->nnodes);
    if(d->firenode != NULL){
      snprintf(name, sizeof(name), "mesh%i.firenode.u8", d->mesh);
      DumpWriteFile(dir, name, d->firenode, (size_t)d->nnodes);
    }
  }
  printf("SMV_DUMP: wrote %s (%i smoke meshes, %i slice pieces, %i vertices)\n", dir, dump.nsmoke, dump.nslice,
         (int)(dump.ntris_bytes / 16));
}
#endif
