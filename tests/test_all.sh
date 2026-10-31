#!/usr/bin/env bash
# ==============================================================================
# Complete Integration Test Suite (Phases 1–5) — University ERP C Server
# Executes:
#   - Phase 1: Basic HTTP & socket communication (test_phase1.sh)
#   - Phase 2: Role-based portal & academic workflow (test_phase2.sh)
#   - Phase 3: POSIX Thread Pool, Mutexes, & Concurrency (test_concurrency.sh)
#   - Phase 4: mmap(), Response Queue, /proc, SIGHUP, Logging (test_phase4.sh)
#   - Phase 5: OS Telemetry, Full CRUD, 50-Request Concurrency Stress (test_phase5.sh)
# ==============================================================================

set -euo pipefail

GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}====================================================================${NC}"
echo -e "${BOLD}      UNIVERSITY ERP — COMPLETE INTEGRATION SUITE (PHASES 1–5)      ${NC}"
echo -e "${BOLD}====================================================================${NC}"

# Ensure executable permissions
chmod +x tests/*.sh

run_suite() {
    local phase_name="$1"
    local script_path="$2"
    
    echo ""
    echo -e "${BLUE}>>> RUNNING ${phase_name}: ${script_path}...${NC}"
    echo "--------------------------------------------------------------------"
    if bash "$script_path"; then
        echo -e "${GREEN}✓ ${phase_name} COMPLETED SUCCESSFULLY!${NC}"
    else
        echo -e "${RED}✗ ${phase_name} FAILED!${NC}"
        exit 1
    fi
}

run_suite "Phase 1: HTTP Socket & Basic Endpoints" "tests/test_phase1.sh"
run_suite "Phase 2: Academic Workflows & Auth" "tests/test_phase2.sh"
run_suite "Phase 3: POSIX Thread Pool & Concurrency" "tests/test_concurrency.sh"
run_suite "Phase 4: Advanced OS Kernel Features (mmap, SIGHUP, /proc)" "tests/test_phase4.sh"
run_suite "Phase 5: OS Monitor Telemetry, CRUD & 50-Req Stress Test" "tests/test_phase5.sh"

echo ""
echo -e "${GREEN}====================================================================${NC}"
echo -e "${GREEN}  🎉 ALL PHASES (1, 2, 3, 4, 5) INTEGRATION TESTS PASSED 100%!       ${NC}"
echo -e "${GREEN}====================================================================${NC}"
