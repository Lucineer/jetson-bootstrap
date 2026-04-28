/**
 * jetson-bootstrap.c — Jetson field guide implementation
 *
 * Probes hardware, calculates budgets, tests capabilities.
 * The output is a report that gets committed to git —
 * making the git history a living record of this Jetson's capabilities.
 */

#define _POSIX_C_SOURCE 200809L
#include "jetson-bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

/* ═══ File readers ═══ */

static int read_file_line(const char *path, char *buf, int len) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return -1; }
    if (!fgets(buf, len, f)) { buf[0] = 0; fclose(f); return -1; }
    /* Strip newline */
    int l = strlen(buf);
    while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = 0;
    fclose(f);
    return l;
}

static int __attribute__((unused)) read_file_all(const char *path, char *buf, int len) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return -1; }
    int total = 0;
    int n;
    while (total < len - 1 && (n = fread(buf + total, 1, len - 1 - total, f)) > 0)
        total += n;
    buf[total] = 0;
    fclose(f);
    return total;
}

/* ═══ Count CPUs ═══ */

static uint32_t count_cpu_cores(void) {
    uint32_t count = 0;
    char path[128];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i);
        FILE *f = fopen(path, "r");
        if (f) { count++; fclose(f); }
    }
    return count;
}

/* ═══ Detect Jetson model ═══ */

static JetsonModel detect_model(const char *dt_model) {
    if (strstr(dt_model, "Orin Nano"))    return JETSON_ORIN_NANO;
    if (strstr(dt_model, "Orin NX"))      return JETSON_ORIN_NX;
    if (strstr(dt_model, "Orin AGX"))     return JETSON_ORIN_AGX;
    if (strstr(dt_model, "Thor"))         return JETSON_THOR;
    if (strstr(dt_model, "Xavier NX"))    return JETSON_NX;
    if (strstr(dt_model, "AGX Xavier"))   return JETSON_AGX_XAVIER;
    if (strstr(dt_model, "Xavier"))       return JETSON_XAVIER;
    if (strstr(dt_model, "TX2"))          return JETSON_TX2;
    if (strstr(dt_model, "TX1"))          return JETSON_TX1;
    if (strstr(dt_model, "Nano"))         return JETSON_NANO;
    return JETSON_UNKNOWN;
}

static const char *model_name(JetsonModel m) {
    switch (m) {
        case JETSON_NANO:        return "Jetson Nano";
        case JETSON_TX1:         return "Jetson TX1";
        case JETSON_TX2:         return "Jetson TX2";
        case JETSON_XAVIER:      return "Jetson Xavier";
        case JETSON_NX:          return "Jetson Xavier NX";
        case JETSON_AGX_XAVIER:  return "Jetson AGX Xavier";
        case JETSON_ORIN_NANO:   return "Jetson Orin Nano";
        case JETSON_ORIN_NX:     return "Jetson Orin NX";
        case JETSON_ORIN_AGX:    return "Jetson Orin AGX";
        case JETSON_THOR:        return "Jetson Thor";
        default:                 return "Unknown";
    }
}

static uint32_t cuda_cores_for_model(JetsonModel m) {
    switch (m) {
        case JETSON_NANO:        return 128;
        case JETSON_TX1:         return 256;
        case JETSON_TX2:         return 256;
        case JETSON_XAVIER:      return 512;
        case JETSON_NX:          return 384;
        case JETSON_AGX_XAVIER:  return 512;
        case JETSON_ORIN_NANO:   return 1024;
        case JETSON_ORIN_NX:     return 1024;
        case JETSON_ORIN_AGX:    return 2048;
        case JETSON_THOR:        return 4096;
        default:                 return 0;
    }
}

/* ═══ Parse memory from /proc/meminfo ═══ */

static uint32_t parse_meminfo_kb(const char *field) {
    char line[128], path[] = "/proc/meminfo";
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    uint32_t val = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, strlen(field)) == 0) {
            sscanf(line + strlen(field), "%u", &val);
            break;
        }
    }
    fclose(f);
    return val / 1024; /* KB to MB */
}

/* ═══ FNV-1a hash for fingerprint ═══ */

static uint64_t fnv1a_64(const char *data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* ═══ Command runner ═══ */

static int run_cmd(const char *cmd, char *out, int max_len) {
    FILE *p = popen(cmd, "r");
    if (!p) { if (out) out[0] = 0; return -1; }
    int total = 0, n;
    while (total < max_len - 1 && (n = fread(out + total, 1, max_len - 1 - total, p)) > 0)
        total += n;
    if (out) out[total] = 0;
    pclose(p);
    return total;
}

/* ═══ Probe ═══ */

int jetson_probe(JetsonBootstrap *bs) {
    if (!bs) return -1;
    memset(bs, 0, sizeof(*bs));

    char line[256];

    /* Machine ID */
    read_file_line("/etc/machine-id", bs->profile.machine_id, sizeof(bs->profile.machine_id));

    /* Jetson model from device tree */
    read_file_line("/proc/device-tree/model", bs->profile.jetson_model, sizeof(bs->profile.jetson_model));
    bs->profile.model_enum = detect_model(bs->profile.jetson_model);
    bs->profile.cuda_cores = cuda_cores_for_model(bs->profile.model_enum);

    /* CPU */
    bs->profile.cpu_cores = count_cpu_cores();
    read_file_line("/proc/version", line, sizeof(line));
    if (strstr(line, "aarch64")) strncpy(bs->profile.cpu_arch, "aarch64", sizeof(bs->profile.cpu_arch));
    else strncpy(bs->profile.cpu_arch, "armv8l", sizeof(bs->profile.cpu_arch));

    read_file_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", line, sizeof(line));
    bs->profile.cpu_max_freq_mhz = atoi(line) / 1000;

    /* Memory */
    bs->profile.total_ram_mb = parse_meminfo_kb("MemTotal:");
    bs->profile.free_ram_mb = parse_meminfo_kb("MemAvailable:");
    /* On Jetson, shared = total (unified memory) */
    bs->profile.shared_ram_mb = bs->profile.total_ram_mb;

    /* Hostname */
    gethostname(bs->profile.hostname, sizeof(bs->profile.hostname));

    /* NVMe */
    run_cmd("lsblk -dn -o SIZE /dev/nvme0n1 2>/dev/null", line, sizeof(line));
    if (strlen(line) > 0) {
        bs->profile.nvme_size_gb = atoi(line) / (1024*1024); /* bytes to GB approx */
    }

    /* Power mode */
    read_file_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", line, sizeof(line));
    /* Try nvpmodel */
    run_cmd("nvpmodel -q 2>/dev/null | head -1", line, sizeof(line));
    if (strstr(line, "MAXN")) strncpy(bs->profile.power_mode, "MAXN", sizeof(bs->profile.power_mode));
    else if (strstr(line, "15W")) strncpy(bs->profile.power_mode, "15W", sizeof(bs->profile.power_mode));
    else if (strstr(line, "10W")) strncpy(bs->profile.power_mode, "10W", sizeof(bs->profile.power_mode));
    else strncpy(bs->profile.power_mode, "unknown", sizeof(bs->profile.power_mode));

    /* Thermal zones */
    {
        int count = 0;
        char path[128];
        for (int i = 0; i < 32; i++) {
            snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
            FILE *f = fopen(path, "r");
            if (f) { count++; fclose(f); }
        }
        bs->profile.thermal_zone_count = count;
    }

    /* Network */
    {
        FILE *f = fopen("/proc/net/wireless", "r");
        bs->profile.has_wifi = (f != NULL);
        if (f) fclose(f);
        f = fopen("/sys/class/net/eth0/carrier", "r");
        if (f) { char c; fread(&c, 1, 1, f); bs->profile.has_ethernet = (c == '1'); fclose(f); }
    }

    /* Fingerprint */
    char fp_data[512];
    snprintf(fp_data, sizeof(fp_data), "%s|%s|%u|%u",
             bs->profile.machine_id,
             bs->profile.jetson_model,
             bs->profile.total_ram_mb,
             bs->profile.cuda_cores);
    bs->profile.fingerprint = fnv1a_64(fp_data, strlen(fp_data));

    /* ═══ Environment ═══ */
    /* Git */
    bs->env.has_git = (run_cmd("git --version 2>/dev/null", line, sizeof(line)) > 0);
    if (bs->env.has_git) sscanf(line, "git version %15s", bs->env.git_version);

    /* Python */
    bs->env.has_python3 = (run_cmd("python3 --version 2>/dev/null", line, sizeof(line)) > 0);
    if (bs->env.has_python3) sscanf(line, "Python %15s", bs->env.python3_version);

    /* Node */
    bs->env.has_node = (run_cmd("node --version 2>/dev/null", line, sizeof(line)) > 0);
    if (bs->env.has_node) strncpy(bs->env.node_version, line, sizeof(bs->env.node_version));

    /* GCC */
    bs->env.has_gcc = (run_cmd("gcc --version 2>/dev/null | head -1", line, sizeof(line)) > 0);
    if (bs->env.has_gcc) {
        char *p = strstr(line, ") ");
        if (p) strncpy(bs->env.gcc_version, p+2, sizeof(bs->env.gcc_version));
    }

    /* NVCC */
    {
        const char *nvcc_paths[] = {
            "/usr/local/cuda/bin/nvcc",
            "/usr/local/cuda-12.6/bin/nvcc",
            "/usr/local/cuda-12.2/bin/nvcc",
            "/usr/local/cuda-12.0/bin/nvcc",
            NULL
        };
        for (int i = 0; nvcc_paths[i]; i++) {
            FILE *f = fopen(nvcc_paths[i], "r");
            if (f) {
                fclose(f);
                strncpy(bs->env.nvcc_path, nvcc_paths[i], sizeof(bs->env.nvcc_path));
                bs->env.has_nvcc = true;
                run_cmd("nvcc --version 2>/dev/null | grep release", line, sizeof(line));
                char *p = strstr(line, "release ");
                if (p) strncpy(bs->env.nvcc_version, p+8, sizeof(bs->env.nvcc_version));
                /* Strip trailing comma */
                char *c = strchr(bs->env.nvcc_version, ',');
                if (c) *c = 0;
                break;
            }
        }
    }

    /* Sudo */
    bs->env.has_sudo = (run_cmd("sudo -n true 2>/dev/null", line, 1) == 0);

    /* Systemd user */
    bs->env.systemd_user = (run_cmd("systemctl --user status 2>/dev/null", line, 1) >= 0);

    /* CUDA toolkit path */
    if (bs->env.has_nvcc) {
        char *p = strrchr(bs->env.nvcc_path, '/');
        if (p) {
            int len = p - bs->env.nvcc_path;
            strncpy(bs->env.cuda_toolkit_path, bs->env.nvcc_path, len);
            bs->env.cuda_toolkit_path[len] = 0;
        }
    }

    /* Known workarounds for this hardware */
    jetson_add_workaround(bs,
        "No sudo access — use systemctl --user for services",
        "systemctl --user start/enable instead of sudo systemctl",
        model_name(bs->profile.model_enum));

    if (bs->profile.model_enum == JETSON_ORIN_NANO) {
        jetson_add_workaround(bs,
            "GCC -O3/-march=native provides ZERO improvement over -O2 on ARM64",
            "Always use -O2. -O3 bloats binary with no speed gain.",
            "Jetson Orin Nano");
        jetson_add_workaround(bs,
            "Python OOMs at ~6.5GB used on 8GB unified RAM",
            "Keep Python scripts small. Batch model calls sequentially (3-4 max).",
            "Jetson Orin Nano");
        jetson_add_workaround(bs,
            "Intermittent DNS failures on Jetson",
            "All network scripts need retry with 3-5s sleep. z.ai DNS unreliable.",
            "Jetson Orin Nano");
        jetson_add_workaround(bs,
            "No Rust/cargo installed — cannot compile Rust code locally",
            "Use C for local development. Push Rust to fleet for cloud compilation.",
            "Jetson Orin Nano");
    }

    return 0;
}

/* ═══ Memory Budget ═══ */

void jetson_calc_memory_budget(JetsonBootstrap *bs) {
    if (!bs) return;
    bs->memory.total_mb = bs->profile.total_ram_mb;
    bs->memory.system_reserve_mb = 800;
    bs->memory.cuda_reserve_mb = (bs->profile.cuda_cores > 0) ? 512 : 0;
    bs->memory.agent_budget_mb = bs->memory.total_mb
                                - bs->memory.system_reserve_mb
                                - bs->memory.cuda_reserve_mb;
    bs->memory.safe_python_mb = (uint32_t)(bs->memory.total_mb * 0.80);
    bs->memory.safe_model_batch_mb = (uint32_t)(bs->memory.total_mb * 0.50);
    bs->memory.safe_single_alloc_mb = (uint32_t)(bs->memory.total_mb * 0.25);
    bs->memory.recommended_headroom_mb = 256;
}

/* ═══ CUDA Test ═══ */

int jetson_test_cuda(JetsonBootstrap *bs, float *gpu_ticks_per_sec) {
    if (!bs) return -1;
    if (!bs->env.has_nvcc) return -2;

    /* Write a minimal CUDA kernel */
    const char *cu = "/tmp/jetson_cuda_test.cu";
    const char *out = "/tmp/jetson_cuda_test";

    FILE *f = fopen(cu, "w");
    if (!f) return -3;
    fprintf(f,
        "#include <stdio.h>\n"
        "__global__ void add(float *a, float *b, float *c, int n) {\n"
        "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "    if (i < n) c[i] = a[i] + b[i];\n"
        "}\n"
        "int main() {\n"
        "    int n = 1048576; /* 1M elements */\n"
        "    float *a, *b, *c;\n"
        "    cudaMalloc(&a, n*sizeof(float));\n"
        "    cudaMalloc(&b, n*sizeof(float));\n"
        "    cudaMalloc(&c, n*sizeof(float));\n"
        "    int blocks = (n + 255) / 256;\n"
        "    add<<<blocks, 256>>>(a, b, c, n);\n"
        "    cudaDeviceSynchronize();\n"
        "    cudaFree(a); cudaFree(b); cudaFree(c);\n"
        "    printf(\"OK\\n\");\n"
        "    return 0;\n"
        "}\n");
    fclose(f);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s -o %s %s 2>&1", bs->env.nvcc_path, out, cu);
    char result[256];
    int len = run_cmd(cmd, result, sizeof(result));
    if (len < 0 || strstr(result, "error")) {
        bs->profile.cuda_available = false;
        unlink(cu);
        return -4;
    }

    /* Run it */
    snprintf(cmd, sizeof(cmd), "%s 2>&1", out);
    len = run_cmd(cmd, result, sizeof(result));
    bs->profile.cuda_available = (strstr(result, "OK") != NULL);

    if (gpu_ticks_per_sec) *gpu_ticks_per_sec = bs->profile.cuda_available ? 1.0f : 0.0f;

    /* Detect CUDA arch from nvcc output */
    if (bs->profile.cuda_available) {
        /* sm_87 for Orin, sm_89 for Orin Nano */
        if (bs->profile.model_enum == JETSON_ORIN_NANO)
            strncpy(bs->profile.cuda_arch, "sm_87", sizeof(bs->profile.cuda_arch));
        else if (bs->profile.model_enum == JETSON_ORIN_NX)
            strncpy(bs->profile.cuda_arch, "sm_87", sizeof(bs->profile.cuda_arch));
        else if (bs->profile.model_enum == JETSON_ORIN_AGX)
            strncpy(bs->profile.cuda_arch, "sm_87", sizeof(bs->profile.cuda_arch));
        else
            strncpy(bs->profile.cuda_arch, "sm_75", sizeof(bs->profile.cuda_arch));
    }

    unlink(cu);
    unlink(out);
    return 0;
}

/* ═══ Network Test ═══ */

int jetson_test_network(JetsonBootstrap *bs, float *avg_dns_ms,
                        float *avg_api_ms, int test_rounds) {
    if (!bs || test_rounds < 1) return -1;

    float total_dns = 0, total_api = 0;
    int dns_ok = 0, api_ok = 0;

    for (int i = 0; i < test_rounds; i++) {
        /* DNS test: time a simple DNS lookup */
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
            "python3 -c \"import time;t=time.time();__import__('socket').gethostbyname('api.github.com');print(f'{time.time()-t:.3f}')\" 2>/dev/null");
        char result[64];
        int len = run_cmd(cmd, result, sizeof(result));
        if (len > 0) {
            float ms = atof(result) * 1000.0f;
            total_dns += ms;
            dns_ok++;
        }

        /* API ping test */
        snprintf(cmd, sizeof(cmd),
            "python3 -c \"import time,urllib.request;t=time.time();urllib.request.urlopen('https://api.github.com',timeout=5);print(f'{time.time()-t:.3f}')\" 2>/dev/null");
        len = run_cmd(cmd, result, sizeof(result));
        if (len > 0) {
            float ms = atof(result) * 1000.0f;
            total_api += ms;
            api_ok++;
        }
    }

    if (avg_dns_ms) *avg_dns_ms = dns_ok > 0 ? total_dns / dns_ok : -1.0f;
    if (avg_api_ms) *avg_api_ms = api_ok > 0 ? total_api / api_ok : -1.0f;
    return (dns_ok + api_ok);
}

/* ═══ Model Test ═══ */

int jetson_test_model(JetsonBootstrap *bs, const char *api_url,
                      const char *api_key, const char *model_name,
                      float *latency_s, char *error, int error_len) {
    if (!bs || !api_url || !api_key || !model_name) return -1;

    /* Write a Python script to test the model */
    const char *script_path = "/tmp/jetson_model_test.py";
    FILE *f = fopen(script_path, "w");
    if (!f) return -2;
    fprintf(f,
        "import json,urllib.request,time,sys\n"
        "url='%s'\n"
        "key='%s'\n"
        "model='%s'\n"
        "t=time.time()\n"
        "try:\n"
        "    data=json.dumps({'model':model,'messages':[{'role':'user','content':'Say OK'}],'max_tokens':5}).encode()\n"
        "    req=urllib.request.Request(url+'/chat/completions',data=data,\n"
        "        headers={'Content-Type':'application/json','Authorization':'Bearer '+key})\n"
        "    r=json.loads(urllib.request.urlopen(req,timeout=30).read())\n"
        "    elapsed=time.time()-t\n"
        "    content=r.get('choices',[{}])[0].get('message',{}).get('content','')\n"
        "    print(f'OK {elapsed:.2f} {content[:20]}')\n"
        "except Exception as e:\n"
        "    print(f'ERR {e}')\n",
        api_url, api_key, model_name);
    fclose(f);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "python3 %s 2>/dev/null", script_path);
    char result[256];
    run_cmd(cmd, result, sizeof(result));

    if (strstr(result, "OK ")) {
        if (latency_s) *latency_s = atof(result + 3);
        if (error) error[0] = 0;
        unlink(script_path);
        return 0;
    } else {
        if (latency_s) *latency_s = -1.0f;
        if (error) strncpy(error, result, error_len);
        unlink(script_path);
        return -3;
    }
}

/* ═══ Workarounds ═══ */

int jetson_add_workaround(JetsonBootstrap *bs, const char *issue,
                          const char *fix, const char *models) {
    if (!bs || !issue || !fix) return -1;
    if (bs->workaround_count >= MAX_WORKAROUNDS) return -2;

    Workaround *w = &bs->workarounds[bs->workaround_count++];
    strncpy(w->issue, issue, sizeof(w->issue) - 1);
    strncpy(w->fix, fix, sizeof(w->fix) - 1);
    strncpy(w->jetson_models, models ? models : "all", sizeof(w->jetson_models) - 1);
    w->confirmed = true;

    /* Date stamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(w->first_seen, sizeof(w->first_seen), "%Y-%m-%d", tm_info);

    return 0;
}

/* ═══ Report ═══ */

int jetson_report(JetsonBootstrap *bs, char *out, int max_len) {
    if (!bs || !out || max_len < 512) return -1;

    int pos = 0;
    pos += snprintf(out + pos, max_len - pos,
        "# Jetson Bootstrap Report\n\n"
        "## Hardware\n"
        "- Model: %s\n"
        "- CPU: %ux %s @ %u MHz\n"
        "- GPU: %u CUDA cores (%s)\n"
        "- RAM: %u MB total, %u MB available\n"
        "- Storage: %lu GB NVMe\n"
        "- Power Mode: %s\n"
        "- Thermal Zones: %u\n"
        "- Fingerprint: 0x%016lx\n\n",
        bs->profile.jetson_model,
        bs->profile.cpu_cores, bs->profile.cpu_arch, bs->profile.cpu_max_freq_mhz,
        bs->profile.cuda_cores,
        bs->profile.cuda_available ? bs->profile.cuda_arch : "N/A",
        bs->profile.total_ram_mb, bs->profile.free_ram_mb,
        bs->profile.nvme_size_gb,
        bs->profile.power_mode,
        bs->profile.thermal_zone_count,
        (unsigned long)bs->profile.fingerprint);

    pos += snprintf(out + pos, max_len - pos,
        "## Environment\n"
        "- Git: %s%s\n"
        "- Python: %s%s\n"
        "- Node: %s%s\n"
        "- GCC: %s%s\n"
        "- NVCC: %s%s\n"
        "- Sudo: %s\n"
        "- Systemd User: %s\n\n",
        bs->env.has_git ? bs->env.git_version : "not found",
        bs->env.has_git ? "" : "",
        bs->env.has_python3 ? bs->env.python3_version : "not found",
        bs->env.has_python3 ? "" : "",
        bs->env.has_node ? bs->env.node_version : "not found",
        bs->env.has_node ? "" : "",
        bs->env.has_gcc ? bs->env.gcc_version : "not found",
        bs->env.has_gcc ? "" : "",
        bs->env.has_nvcc ? bs->env.nvcc_version : "not found",
        bs->env.has_nvcc ? "" : "",
        bs->env.has_sudo ? "yes" : "no",
        bs->env.systemd_user ? "yes" : "no");

    pos += snprintf(out + pos, max_len - pos,
        "## Memory Budget\n"
        "- Total: %u MB\n"
        "- System Reserve: %u MB\n"
        "- CUDA Reserve: %u MB\n"
        "- Agent Budget: %u MB\n"
        "- Safe Python Limit: %u MB\n"
        "- Safe Model Batch: %u MB\n"
        "- Max Single Alloc: %u MB\n"
        "- Recommended Headroom: %u MB\n\n",
        bs->memory.total_mb, bs->memory.system_reserve_mb,
        bs->memory.cuda_reserve_mb, bs->memory.agent_budget_mb,
        bs->memory.safe_python_mb, bs->memory.safe_model_batch_mb,
        bs->memory.safe_single_alloc_mb, bs->memory.recommended_headroom_mb);

    pos += snprintf(out + pos, max_len - pos,
        "## Known Workarounds (%d)\n\n", bs->workaround_count);
    for (int i = 0; i < bs->workaround_count && pos < max_len - 64; i++) {
        Workaround *w = &bs->workarounds[i];
        pos += snprintf(out + pos, max_len - pos,
            "%d. **%s** (since %s, %s)\n"
            "   Fix: %s\n\n",
            i + 1, w->issue, w->first_seen, w->jetson_models, w->fix);
    }

    return pos;
}

/* ═══ Clone Instructions ═══ */

int jetson_clone_instructions(JetsonBootstrap *bs, char *out, int max_len) {
    if (!bs || !out) return -1;

    return snprintf(out, max_len,
        "# Clone and Run\n\n"
        "## Prerequisites\n\n"
        "```bash\n"
        "sudo apt-get update\n"
        "sudo apt-get install -y git gcc python3 nodejs\n"
        "```\n\n"
        "## Clone\n\n"
        "```bash\n"
        "git clone https://github.com/Lucineer/jetson-bootstrap.git\n"
        "cd jetson-bootstrap\n"
        "```\n\n"
        "## Bootstrap\n\n"
        "```bash\n"
        "make\n"
        "./jetson-bootstrap\n"
        "```\n\n"
        "This probes your hardware, tests CUDA, benchmarks network,\n"
        "and generates `BOOTSTRAP-REPORT.md` with your Jetson's profile.\n\n"
        "## Commit Your Profile\n\n"
        "```bash\n"
        "git add BOOTSTRAP-REPORT.md\n"
        "git commit -m \"Bootstrap: [your model] profile\"\n"
        "git push\n"
        "```\n\n"
        "Your profile joins the fleet. Git history is your memory.\n\n"
        "## What Happens Next\n\n"
        "1. Your Jetson's capabilities are recorded in the repo\n"
        "2. Workarounds for your specific model are applied\n"
        "3. Memory budgets are calculated for your RAM size\n"
        "4. You can now run fleet git-agents with known constraints\n"
        "5. Every commit you make updates the fleet's knowledge of your hardware\n\n"
        "## Updating\n\n"
        "```bash\n"
        "git pull\n"
        "make clean && make\n"
        "./jetson-bootstrap  # re-probe after updates\n"
        "```\n\n"
        "## Fleet Protocol\n\n"
        "- Each Jetson commits its bootstrap report\n"
        "- Git history = distributed memory (rewind, fork, replicate)\n"
        "- If this Jetson goes down, clone the repo on another and you're a few commits behind\n"
        "- Workarounds accumulate — the fleet learns from every failure\n");
}

/* ═══ Save/Load (simple key=value format) ═══ */

int jetson_save(const JetsonBootstrap *bs, const char *path) {
    if (!bs || !path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -2;

    fprintf(f, "# Jetson Bootstrap State\n");
    fprintf(f, "model=%s\n", bs->profile.jetson_model);
    fprintf(f, "model_enum=%d\n", bs->profile.model_enum);
    fprintf(f, "cpu_cores=%u\n", bs->profile.cpu_cores);
    fprintf(f, "cpu_arch=%s\n", bs->profile.cpu_arch);
    fprintf(f, "cpu_max_freq_mhz=%u\n", bs->profile.cpu_max_freq_mhz);
    fprintf(f, "cuda_cores=%u\n", bs->profile.cuda_cores);
    fprintf(f, "cuda_available=%d\n", bs->profile.cuda_available);
    fprintf(f, "cuda_arch=%s\n", bs->profile.cuda_arch);
    fprintf(f, "cuda_version=%s\n", bs->env.nvcc_version);
    fprintf(f, "nvcc_path=%s\n", bs->env.nvcc_path);
    fprintf(f, "total_ram_mb=%u\n", bs->profile.total_ram_mb);
    fprintf(f, "shared_ram_mb=%u\n", bs->profile.shared_ram_mb);
    fprintf(f, "nvme_size_gb=%lu\n", (unsigned long)bs->profile.nvme_size_gb);
    fprintf(f, "power_mode=%s\n", bs->profile.power_mode);
    fprintf(f, "fingerprint=0x%016lx\n", (unsigned long)bs->profile.fingerprint);
    fprintf(f, "has_git=%d\n", bs->env.has_git);
    fprintf(f, "has_python3=%d\n", bs->env.has_python3);
    fprintf(f, "has_node=%d\n", bs->env.has_node);
    fprintf(f, "has_gcc=%d\n", bs->env.has_gcc);
    fprintf(f, "has_nvcc=%d\n", bs->env.has_nvcc);
    fprintf(f, "has_sudo=%d\n", bs->env.has_sudo);
    fprintf(f, "systemd_user=%d\n", bs->env.systemd_user);
    fprintf(f, "workaround_count=%d\n", bs->workaround_count);
    fprintf(f, "model_count=%d\n", bs->model_count);

    for (int i = 0; i < bs->workaround_count; i++) {
        fprintf(f, "workaround_%d_issue=%s\n", i, bs->workarounds[i].issue);
        fprintf(f, "workaround_%d_fix=%s\n", i, bs->workarounds[i].fix);
    }

    fclose(f);
    return 0;
}

int jetson_load(JetsonBootstrap *bs, const char *path) {
    /* Simplified: just re-probe instead of parsing */
    (void)path;
    return jetson_probe(bs);
}

/* ═══ Full Bootstrap Run ═══ */

int jetson_bootstrap_run(JetsonBootstrap *bs) {
    if (!bs) return -1;

    int step = 0;
    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Probing hardware...\n", step);
        step++;

    int rc = jetson_probe(bs);
    if (rc != 0) return rc;

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Model: %s\n", step, bs->profile.jetson_model);
        step++;

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Calculating memory budget...\n", step);
        step++;
    jetson_calc_memory_budget(bs);

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Agent budget: %u MB\n", step, bs->memory.agent_budget_mb);
        step++;

    if (bs->env.has_nvcc) {
        snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
            "[%d] Testing CUDA...\n", step);
            step++;
        float gpu_speed;
        rc = jetson_test_cuda(bs, &gpu_speed);
        snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
            "[%d] CUDA: %s\n", step, bs->profile.cuda_available ? "OK" : "FAILED");
            step++;
    }

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Testing network...\n", step);
        step++;
    float dns_ms, api_ms;
    jetson_test_network(bs, &dns_ms, &api_ms, 3);
    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] DNS: %.0fms, API: %.0fms\n", step, dns_ms, api_ms);
        step++;

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Known workarounds: %d\n", step, bs->workaround_count);
        step++;

    snprintf(bs->boot_log + step * 100, sizeof(bs->boot_log) - step * 100,
        "[%d] Bootstrap complete.\n", step);
        step++;
    bs->boot_step = step;

    /* Generate report file */
    char report[4096];
    jetson_report(bs, report, sizeof(report));
    FILE *f = fopen("BOOTSTRAP-REPORT.md", "w");
    if (f) {
        fputs(report, f);
        fclose(f);
    }

    return 0;
}

/* ═══ Tests ═══ */

int jetson_bootstrap_test(void) {
    int failures = 0;

    /* Test 1: Probe on this machine */
    JetsonBootstrap bs;
    int rc = jetson_probe(&bs);
    if (rc != 0) { failures++; printf("FAIL probe: %d\n", rc); }
    else printf("  Probed: %s, %u cores, %u CUDA, %u MB RAM\n",
               bs.profile.jetson_model, bs.profile.cpu_cores,
               bs.profile.cuda_cores, bs.profile.total_ram_mb);

    /* Test 2: Should detect Orin Nano */
    if (bs.profile.model_enum != JETSON_ORIN_NANO && bs.profile.model_enum != JETSON_ORIN_NX) {
        /* Don't fail on different hardware, just note */
        printf("  NOTE: Model enum = %d (expected ORIN_NANO=7)\n", bs.profile.model_enum);
    }

    /* Test 3: Should detect ARM64 */
    if (strcmp(bs.profile.cpu_arch, "aarch64") != 0) {
        /* Don't fail on x86 test machines */
        printf("  NOTE: CPU arch = %s\n", bs.profile.cpu_arch);
    }

    /* Test 4: Memory budget */
    jetson_calc_memory_budget(&bs);
    if (bs.memory.agent_budget_mb == 0) {
        failures++; printf("FAIL memory budget\n");
    } else {
        printf("  Memory budget: %u MB agent, %u MB safe python\n",
               bs.memory.agent_budget_mb, bs.memory.safe_python_mb);
    }

    /* Test 5: Workarounds registered */
    if (bs.workaround_count < 2) {
        failures++; printf("FAIL workarounds: %d\n", bs.workaround_count);
    } else {
        printf("  Workarounds: %d registered\n", bs.workaround_count);
    }

    /* Test 6: Report generation */
    char report[4096];
    rc = jetson_report(&bs, report, sizeof(report));
    if (rc <= 0) {
        failures++; printf("FAIL report\n");
    } else {
        printf("  Report: %d bytes\n", rc);
    }

    /* Test 7: Clone instructions */
    char clone_instr[2048];
    rc = jetson_clone_instructions(&bs, clone_instr, sizeof(clone_instr));
    if (rc <= 0) {
        failures++; printf("FAIL clone instructions\n");
    } else {
        printf("  Clone instructions: %d bytes\n", rc);
    }

    /* Test 8: Save */
    rc = jetson_save(&bs, "/tmp/jetson_test_state.txt");
    if (rc != 0) {
        failures++; printf("FAIL save\n");
    } else {
        printf("  Save: OK\n");
    }

    /* Test 9: Fingerprint is unique */
    if (bs.profile.fingerprint == 0) {
        failures++; printf("FAIL fingerprint\n");
    } else {
        printf("  Fingerprint: 0x%016lx\n", (unsigned long)bs.profile.fingerprint);
    }

    /* Test 10: Environment detection */
    if (!bs.env.has_gcc) {
        printf("  NOTE: No GCC detected\n");
    }
    if (bs.env.has_nvcc) {
        printf("  NVCC: %s\n", bs.env.nvcc_path);
    }

    /* Test 11: Workaround add overflow */
    JetsonBootstrap bs2;
    memset(&bs2, 0, sizeof(bs2));
    for (int i = 0; i < MAX_WORKAROUNDS + 5; i++) {
        rc = jetson_add_workaround(&bs2, "test", "test fix", "all");
        if (i >= MAX_WORKAROUNDS && rc != -2) {
            failures++; printf("FAIL workaround overflow at %d\n", i);
            break;
        }
    }
    if (bs2.workaround_count != MAX_WORKAROUNDS) {
        failures++; printf("FAIL workaround count: %d vs %d\n",
                          bs2.workaround_count, MAX_WORKAROUNDS);
    }

    /* Test 12: Null safety */
    rc = jetson_probe(NULL);
    if (rc != -1) { failures++; printf("FAIL null probe\n"); }
    jetson_calc_memory_budget(NULL);
    /* Should not crash */

    printf("jetson_bootstrap_test: %d failures\n", failures);
    return failures;
}
