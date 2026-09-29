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
#define DUMP_FACES 3

// GL state blockage faces are lit with, read after the draw (glGet is not allowed inside glBegin)
typedef struct {
  int use_lighting, light_faces, cull, cull_mode, front_face, shade_model, normalize, rescale;
  int color_material_face, color_material_mode, two_side, local_viewer, light_enabled[2];
  float light_position[2][4], light_ambient[2][4], light_diffuse[2][4], light_specular[2][4], model_ambient[4];
  float mat_specular[4], mat_emission[4], mat_shininess;
} dump_lighting;

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
  unsigned int prim;
  float normal[3], rgba[4];
  unsigned char *faces;
  size_t nfaces_bytes, capfaces;
  float *feedback;
  int nfeedback, capfeedback;
  int has_lighting;
  dump_lighting lighting;
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
  dump.nfaces_bytes = 0;
  dump.nfeedback = 0;
  dump.has_lighting = 0;
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

void SmvDumpFacesBegin(void){
  CaptureCamera();
  dump.mode = DUMP_FACES;
}

static void GetLight(int i, GLenum light){
  dump_lighting *l = &dump.lighting;

  l->light_enabled[i] = glIsEnabled(light);
  glGetLightfv(light, GL_POSITION, l->light_position[i]);
  glGetLightfv(light, GL_AMBIENT, l->light_ambient[i]);
  glGetLightfv(light, GL_DIFFUSE, l->light_diffuse[i]);
  glGetLightfv(light, GL_SPECULAR, l->light_specular[i]);
}

void SmvDumpFacesEnd(void){
  dump_lighting *l = &dump.lighting;
  GLint v;

  dump.mode = DUMP_NONE;
  if(dump.has_lighting) return;
  dump.has_lighting = 1;
  l->use_lighting = use_lighting;
  l->light_faces = light_faces;
  l->cull = glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_CULL_FACE_MODE, &v); l->cull_mode = v;
  glGetIntegerv(GL_FRONT_FACE, &v); l->front_face = v;
  glGetIntegerv(GL_SHADE_MODEL, &v); l->shade_model = v;
  l->normalize = glIsEnabled(GL_NORMALIZE);
  l->rescale = glIsEnabled(GL_RESCALE_NORMAL);
  glGetIntegerv(GL_COLOR_MATERIAL_FACE, &v); l->color_material_face = v;
  glGetIntegerv(GL_COLOR_MATERIAL_PARAMETER, &v); l->color_material_mode = v;
  glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE, &v); l->two_side = v;
  glGetIntegerv(GL_LIGHT_MODEL_LOCAL_VIEWER, &v); l->local_viewer = v;
  glGetFloatv(GL_LIGHT_MODEL_AMBIENT, l->model_ambient);
  GetLight(0, GL_LIGHT0);
  GetLight(1, GL_LIGHT1);
  glGetMaterialfv(GL_FRONT, GL_SPECULAR, l->mat_specular);
  glGetMaterialfv(GL_FRONT, GL_EMISSION, l->mat_emission);
  glGetMaterialfv(GL_FRONT, GL_SHININESS, &l->mat_shininess);
}

// Draws again into a feedback buffer: GL_3D_COLOR gives each vertex that survives culling and
// clipping as window x, y, z and its lit RGBA. Nothing is rasterized, so the image is unchanged.
void SmvDumpFacesFeedback(void (*draw)(int), int option){
  static GLfloat *buf = NULL;
  const GLsizei cap = 1 << 22;
  GLint n;

  if(buf == NULL) buf = malloc((size_t)cap * sizeof(GLfloat));
  if(buf == NULL) return;
  glFeedbackBuffer(cap, GL_3D_COLOR, buf);
  glRenderMode(GL_FEEDBACK);
  draw(option);
  n = glRenderMode(GL_RENDER);
  if(n < 0){
    fprintf(stderr, "*** SMV_DUMP: feedback buffer overflow\n");
    return;
  }
  dump.feedback = Grow(dump.feedback, &dump.capfeedback, dump.nfeedback + n, sizeof(float));
  memcpy(dump.feedback + dump.nfeedback, buf, (size_t)n * sizeof(float));
  dump.nfeedback += n;
}

void SmvDumpBegin(unsigned int mode){
  dump.prim = mode;
  glBegin(mode);
}

void SmvDumpNormal3fv(const float *n){
  memcpy(dump.normal, n, 3 * sizeof(float));
  glNormal3fv(n);
}

void SmvDumpColor3fv(const float *c){
  memcpy(dump.rgba, c, 3 * sizeof(float));
  dump.rgba[3] = 1.0f;
  glColor3fv(c);
}

void SmvDumpColor4fv(const float *c){
  memcpy(dump.rgba, c, 4 * sizeof(float));
  glColor4fv(c);
}

// 44 bytes per vertex: x, y, z, normal, RGBA as f32, then the primitive as u32
void SmvDumpVertex3fv(const float *v){
  if(dump.mode == DUMP_FACES){
    unsigned char *r;
    int cap = (int)dump.capfaces;

    dump.faces = Grow(dump.faces, &cap, (int)(dump.nfaces_bytes + 44), 1);
    dump.capfaces = (size_t)cap;
    r = dump.faces + dump.nfaces_bytes;
    memcpy(r, v, 12);
    memcpy(r + 12, dump.normal, 12);
    memcpy(r + 24, dump.rgba, 16);
    memcpy(r + 40, &dump.prim, 4);
    dump.nfaces_bytes += 44;
  }
  glVertex3fv(v);
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
  fprintf(f, "%s],\n  \"tris_vertices\": %i,\n", dump.nslice ? "\n  " : "", (int)(dump.ntris_bytes / 16));
  fprintf(f, "  \"obst_vertices\": %i,\n  \"obst_feedback_values\": %i", (int)(dump.nfaces_bytes / 44), dump.nfeedback);
  if(dump.has_lighting){
    const dump_lighting *l = &dump.lighting;

    fprintf(f, ",\n  \"lighting\": {\"use_lighting\": %i, \"light_faces\": %i, \"cull\": %i, \"cull_mode\": %i, "
            "\"front_face\": %i, \"shade_model\": %i, \"normalize\": %i, \"rescale_normal\": %i, "
            "\"color_material_face\": %i, \"color_material_mode\": %i, \"two_side\": %i, \"local_viewer\": %i,\n",
            l->use_lighting, l->light_faces, l->cull, l->cull_mode, l->front_face, l->shade_model, l->normalize, l->rescale,
            l->color_material_face, l->color_material_mode, l->two_side, l->local_viewer);
    fprintf(f, "    \"model_ambient\": ");
    JsonFloats(f, l->model_ambient, 4);
    for(i = 0; i < 2; i++){
      fprintf(f, ",\n    \"light%i\": {\"enabled\": %i, \"position\": ", i, l->light_enabled[i]);
      JsonFloats(f, l->light_position[i], 4);
      fprintf(f, ", \"ambient\": ");
      JsonFloats(f, l->light_ambient[i], 4);
      fprintf(f, ", \"diffuse\": ");
      JsonFloats(f, l->light_diffuse[i], 4);
      fprintf(f, ", \"specular\": ");
      JsonFloats(f, l->light_specular[i], 4);
      fprintf(f, "}");
    }
    fprintf(f, ",\n    \"material_specular\": ");
    JsonFloats(f, l->mat_specular, 4);
    fprintf(f, ", \"material_emission\": ");
    JsonFloats(f, l->mat_emission, 4);
    fprintf(f, ", \"material_shininess\": %.9g}", l->mat_shininess);
  }
  fprintf(f, "\n}\n");
  fclose(f);

  DumpWriteFile(dir, "rgb_slice.f32", rgb_slice, sizeof(rgb_slice));
  DumpWriteFile(dir, "smoke_cmap.f32", rgb_slicesmokecolormap_01, sizeof(rgb_slicesmokecolormap_01));
  DumpWriteFile(dir, "tris.bin", dump.tris, dump.ntris_bytes);
  DumpWriteFile(dir, "obst.bin", dump.faces, dump.nfaces_bytes);
  DumpWriteFile(dir, "obst_feedback.f32", dump.feedback, (size_t)dump.nfeedback * sizeof(float));
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
  printf("SMV_DUMP: wrote %s (%i smoke meshes, %i slice pieces, %i vertices, %i face vertices)\n", dir, dump.nsmoke,
         dump.nslice, (int)(dump.ntris_bytes / 16), (int)(dump.nfaces_bytes / 44));
}
#endif
