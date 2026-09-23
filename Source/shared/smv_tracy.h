#ifndef SMV_TRACY_H_DEFINED
#define SMV_TRACY_H_DEFINED

// Tracy zones matching smokeshow's smvio, joined by zone name (smokeshow docs/profiling.md).
// Include and use only under #ifdef SMV_TRACY, so an OFF build preprocesses to upstream.
#ifdef SMV_TRACY
#ifndef TRACY_ENABLE
#error "SMV_TRACY needs TRACY_ENABLE and the Tracy client"
#endif
#include <stdio.h>
#include "tracy/TracyC.h"

// Scoped zone: one per block, SMVZONE_END on every path out
#define SMVZONE(name)  TracyCZoneN(__smvz, name, 1)
#define SMVZONE_END()  TracyCZoneEnd(__smvz)

// Zone held in a variable, for spans that do not match a block. A zeroed context is
// inactive, so closing one that is not open is a no-op.
#define SMVZONE_CTX TracyCZoneCtx
#define SMVZONE_OPEN(ctx, name) do{\
  static const struct ___tracy_source_location_data smv_loc = { name, __func__, __FILE__, (uint32_t)__LINE__, 0 };\
  (ctx) = ___tracy_emit_zone_begin(&smv_loc, 1);\
}while(0)
#define SMVZONE_CLOSE(ctx) do{ ___tracy_emit_zone_end(ctx); (ctx).active = 0; }while(0)

// Resident set size, one sample per dataset
#define SMVPLOT_RSS() TracyCPlot("rss", (double)SmvTracyRss())

size_t SmvTracyFread(void *ptr, size_t size, size_t count, FILE *stream);
unsigned long long SmvTracyRss(void);

#endif
#endif
