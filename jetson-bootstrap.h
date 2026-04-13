/**
 * jetson-bootstrap.h — Jetson field guide for git-agent replication
 *
 * A git-agent that another Jetson can clone and be operational.
 * Git history IS the memory. Clone = few hours behind, not starting over.
 *
 * This captures:
 * 1. Hardware probing (what Jetson am I on?)
 * 2. Environment bootstrap (what do I need installed?)
 * 3. Capability mapping (what can I actually do?)
 * 4. Workarounds database (what breaks and how to fix it)
 * 5. Model benchmarking (what runs in 8GB unified RAM?)
 * 6. CUDA readiness (does nvcc work? how fast?)
 * 7. Network resilience (DNS hiccups, retries, timeouts)
 * 8. Memory budget (what's safe? what OOMs?)
 *
 * Zero deps. C99. Runs on any Linux ARM64.
 */

#ifndef JETSON_BOOTSTRAP_H
#define JETSON_BOOTSTRAP_H

#include <stdint.h>
#include <stdbool.h>

/* ═══ Jetson Models ═══ */
typedef enum {
    JETSON_UNKNOWN = 0,
    JETSON_NANO     = 1,
    JETSON_TX1      = 2,
    JETSON_TX2      = 3,
    JETSON_XAVIER   = 4,
    JETSON_NX       = 5,
    JETSON_AGX_XAVIER = 6,
    JETSON_ORIN_NANO  = 7,
    JETSON_ORIN_NX    = 8,
    JETSON_ORIN_AGX   = 9,
    JETSON_THOR       = 10
} JetsonModel;

/* ═══ Hardware Profile ═══ */
typedef struct {
    /* Identity */
    char machine_id[33];          /* /etc/machine-id */
    char jetson_model[64];        /* from /proc/device-tree/model */
    JetsonModel model_enum;

    /* CPU */
    uint32_t cpu_cores;
    char cpu_arch[32];            /* armv8l, aarch64 */
    uint32_t cpu_max_freq_mhz;    /* from /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq */

    /* GPU */
    uint32_t cuda_cores;
    char cuda_arch[16];           /* sm_87, sm_89, etc */
    bool cuda_available;
    char cuda_version[32];        /* 12.6, 12.2, etc */
    char nvcc_path[128];

    /* Memory */
    uint32_t total_ram_mb;        /* from /proc/meminfo MemTotal */
    uint32_t shared_ram_mb;       /* unified memory shared with GPU */
    uint32_t free_ram_mb;         /* current free */

    /* Storage */
    uint64_t nvme_size_gb;
    char root_fs[64];             /* /dev/nvme0n1p1, etc */

    /* Network */
    char hostname[64];
    bool has_wifi;
    bool has_ethernet;
    uint32_t arp_entries;         /* local network devices */

    /* Power */
    char power_mode[16];          /* MAXN, 15W, 10W, etc */
    uint32_t thermal_zone_count;

    /* Unique fingerprint for fleet identification */
    uint64_t fingerprint;
} JetsonProfile;

/* ═══ Environment Status ═══ */
typedef struct {
    /* System */
    bool has_git;
    char git_version[16];
    bool has_python3;
    char python3_version[16];
    bool has_node;
    char node_version[16];
    bool has_gcc;
    char gcc_version[16];
    bool has_nvcc;
    char nvcc_version[16];

    /* Sudo access */
    bool has_sudo;

    /* Systemd */
    bool systemd_user;            /* systemctl --user works */

    /* Useful paths */
    char cuda_toolkit_path[128];
    char nvcc_path[128];
    char tegra_dir[128];

    /* What's missing */
    char missing[256];            /* comma-separated list */
} EnvStatus;

/* ═══ Memory Budget ═══ */
typedef struct {
    uint32_t total_mb;
    uint32_t system_reserve_mb;   /* OS + services ~800MB */
    uint32_t agent_budget_mb;     /* what we can actually use */
    uint32_t safe_python_mb;      /* Python starts swapping here ~6.5GB */
    uint32_t safe_model_batch_mb; /* sequential model calls ~4GB */
    uint32_t cuda_reserve_mb;     /* GPU needs its share */
    uint32_t safe_single_alloc_mb; /* max single allocation */
    uint32_t recommended_headroom_mb; /* keep this much free */
} MemoryBudget;

/* ═══ Model Compatibility ═══ */
typedef struct {
    char model_name[64];
    char provider[32];
    bool fits_8gb;
    bool fits_4gb;
    uint32_t est_ram_mb;
    uint32_t max_input_tokens;
    uint32_t max_output_tokens;
    float avg_latency_s;
    bool reliable;
    char notes[128];
} ModelCompat;

/* ═══ Workaround Entry ═══ */
typedef struct {
    char issue[128];
    char fix[256];
    char first_seen[16];          /* date YYYY-MM-DD */
    char jetson_models[64];       /* which models affected */
    bool confirmed;
} Workaround;

/* ═══ Bootstrap State ═══ */
#define MAX_MODELS      32
#define MAX_WORKAROUNDS 64
#define MAX_SERVICES    16

typedef struct {
    JetsonProfile profile;
    EnvStatus env;
    MemoryBudget memory;

    ModelCompat models[MAX_MODELS];
    int model_count;

    Workaround workarounds[MAX_WORKAROUNDS];
    int workaround_count;

    /* Active git-agent services */
    char services[MAX_SERVICES][64];
    int service_count;

    /* Boot sequence */
    char boot_log[2048];
    int boot_step;
} JetsonBootstrap;

/* ═══ API ═══ */

/** Probe all hardware and environment */
int jetson_probe(JetsonBootstrap *bs);

/** Calculate memory budget based on profile */
void jetson_calc_memory_budget(JetsonBootstrap *bs);

/** Test if CUDA compiles and runs */
int jetson_test_cuda(JetsonBootstrap *bs, float *gpu_ticks_per_sec);

/** Test network reliability (DNS, latency) */
int jetson_test_network(JetsonBootstrap *bs, float *avg_dns_ms,
                        float *avg_api_ms, int test_rounds);

/** Test model compatibility (does provider respond?) */
int jetson_test_model(JetsonBootstrap *bs, const char *api_url,
                      const char *api_key, const char *model_name,
                      float *latency_s, char *error, int error_len);

/** Add a known workaround */
int jetson_add_workaround(JetsonBootstrap *bs, const char *issue,
                          const char *fix, const char *models);

/** Generate bootstrap report (for commit to git) */
int jetson_report(JetsonBootstrap *bs, char *out, int max_len);

/** Generate CLONE_AND_RUN.md instructions */
int jetson_clone_instructions(JetsonBootstrap *bs, char *out, int max_len);

/** Save/load bootstrap state to JSON file */
int jetson_save(const JetsonBootstrap *bs, const char *path);
int jetson_load(JetsonBootstrap *bs, const char *path);

/** Run all tests and produce report */
int jetson_bootstrap_run(JetsonBootstrap *bs);

/** Test suite */
int jetson_bootstrap_test(void);

#endif
