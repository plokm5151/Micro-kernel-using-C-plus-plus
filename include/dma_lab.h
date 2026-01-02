#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Run deterministic DMA coherency lab cases. |mode| selects which cases run.
// - mode=1: run all cases
// - mode=N (N>=2): run a specific case number (best-effort)
void dma_lab_run(unsigned mode);

#ifdef __cplusplus
}
#endif

