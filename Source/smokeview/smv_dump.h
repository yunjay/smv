#ifndef SMV_DUMP_H_DEFINED
#define SMV_DUMP_H_DEFINED

// Render dump for smokeshow's smv2png (smokeshow docs/smv2png-plan.md §5): the state of the draw
// pass that produced a rendered image, written beside nothing else when SMV_DUMP_DIR is set.
// Include and use only under #ifdef SMV_DUMP, so an OFF build preprocesses to upstream.
#ifdef SMV_DUMP

struct _meshdata;
struct _slicedata;
struct _smoke3ddata;

// A draw pass starts (ShowScene); what the previous one recorded is dropped
void SmvDumpPassBegin(void);
// SETTIMEVAL: the time asked for and the global frame it picked
void SmvDumpSetTime(float requested, int iglobal, float global_time);
// One mesh of 3-D smoke, after ADJUSTALPHA and before its triangles; SmvDumpDrawEnd after glEnd
void SmvDumpSmokeMesh(const struct _smoke3ddata *s, const struct _meshdata *m, const unsigned char *alpha_map,
                      const unsigned char *fire_map, const unsigned char *alpha_out);
// One node-centred slice piece, with the bounds its texture coordinates use
void SmvDumpSliceBegin(const struct _slicedata *sd, float valmin, float valmax);
void SmvDumpDrawEnd(void);
// The image of this pass was written: write the dump
void SmvDumpFlush(const char *image_file);

// Forwarding GL wrappers; they record while a mesh or slice piece is being drawn
void SmvDumpColor4ubv(const unsigned char *c);
void SmvDumpTexCoord1f(float t);
void SmvDumpVertex3f(float x, float y, float z);

#endif
#endif
