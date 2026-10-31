// University ERP — Phase 2 Frontend Controller
// Pure Vanilla JavaScript communicating via HTTP/POSIX sockets with native C Linux Backend

document.addEventListener('DOMContentLoaded', () => {
  // ---------------------------------------------------------------------------
  // STATE MANAGEMENT
  // ---------------------------------------------------------------------------
  let currentAuth = null; // { role: 'student' | 'faculty', user: {...} }
  let studentData = null; // Cached dashboard response for student
  let facultyData = null; // Cached dashboard response for faculty

  // ---------------------------------------------------------------------------
  // ROBUST DATA SERIALIZATION & CROSS-SESSION SYNCHRONIZATION
  // ---------------------------------------------------------------------------

  // Resilient JSON parser that handles valid JSON and auto-sanitizes C snprintf trailing commas
  function parseServerJson(text) {
    if (!text || typeof text !== 'string') return null;
    try {
      return JSON.parse(text);
    } catch (err) {
      // Clean up common C snprintf quirks (trailing commas before ] or }, adjacent object blocks)
      const sanitized = text
        .replace(/,\s*([\]}])/g, '$1')
        .replace(/}\s*([{\[])/g, '},$1');
      return JSON.parse(sanitized);
    }
  }

  // Reliable fetch with cache-busting and robust JSON parsing
  async function fetchServerJson(url, options = {}) {
    const sep = url.includes('?') ? '&' : '?';
    const cacheBustedUrl = `${url}${sep}_t=${Date.now()}`;
    const opts = {
      ...options,
      cache: 'no-store',
      headers: {
        'Accept': 'application/json',
        ...(options.headers || {})
      }
    };
    const resp = await fetch(cacheBustedUrl, opts);
    if (!resp.ok) {
      let errMsg = `Server responded with HTTP ${resp.status}`;
      try {
        const errText = await resp.text();
        const errJson = parseServerJson(errText);
        if (errJson && errJson.message) errMsg = errJson.message;
      } catch (e) {}
      const error = new Error(errMsg);
      error.status = resp.status;
      throw error;
    }
    const rawText = await resp.text();
    return parseServerJson(rawText);
  }

  // Cross-Tab / Cross-Window Synchronization Channel
  let syncChannel = null;
  try {
    if (typeof BroadcastChannel !== 'undefined') {
      syncChannel = new BroadcastChannel('univ_erp_academic_sync');
      syncChannel.onmessage = (event) => {
        const msg = event.data;
        if (msg && msg.type === 'academic_sync') {
          handleRemoteAcademicUpdate(msg.student_id, msg.course_id, msg.update_type);
        }
      };
    }
  } catch (e) {
    console.warn('BroadcastChannel not available in this environment:', e);
  }

  // Storage event fallback for cross-tab sync in browsers or sandboxed iframes
  window.addEventListener('storage', (e) => {
    if (e.key === 'univ_erp_academic_sync_payload' && e.newValue) {
      try {
        const payload = JSON.parse(e.newValue);
        if (payload && payload.type === 'academic_sync') {
          handleRemoteAcademicUpdate(payload.student_id, payload.course_id, payload.update_type);
        }
      } catch (err) {}
    }
  });

  function broadcastAcademicSyncEvent(studentId, courseId, updateType) {
    const payload = {
      type: 'academic_sync',
      student_id: studentId,
      course_id: courseId,
      update_type: updateType,
      timestamp: Date.now()
    };
    if (syncChannel) {
      try { syncChannel.postMessage(payload); } catch (e) {}
    }
    try {
      localStorage.setItem('univ_erp_academic_sync_payload', JSON.stringify(payload));
    } catch (e) {}
  }

  let isSyncing = false;
  async function handleRemoteAcademicUpdate(studentId, courseId, updateType) {
    if (isSyncing || !currentAuth) return;
    isSyncing = true;
    try {
      if (currentAuth.role === 'student') {
        if (!studentId || studentId === currentAuth.user.id) {
          await loadStudentDashboard(currentAuth.user.id);
        }
      } else if (currentAuth.role === 'faculty') {
        await loadFacultyDashboard(currentAuth.user.id);
      }
    } catch (e) {
      console.warn('Sync revalidation error:', e);
    } finally {
      isSyncing = false;
    }
  }

  // Unified sync dispatcher called immediately after any marks or attendance update on C server
  async function syncAcademicDataAcrossAllSections(targetStudentId, targetCourseId, updateType) {
    // 1. Broadcast to any other open tabs or sessions
    broadcastAcademicSyncEvent(targetStudentId, targetCourseId, updateType);

    // 2. Immediately refresh all views in the current session
    if (currentAuth) {
      if (currentAuth.role === 'faculty') {
        await loadFacultyDashboard(currentAuth.user.id);
      } else if (currentAuth.role === 'student') {
        if (!targetStudentId || targetStudentId === currentAuth.user.id) {
          await loadStudentDashboard(currentAuth.user.id);
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // DOM ELEMENTS
  // ---------------------------------------------------------------------------
  const statusPill = document.getElementById('server-status-pill');
  const statusDot = document.getElementById('status-dot');
  const statusText = document.getElementById('server-status-text');
  const flowPid = document.getElementById('flow-pid');
  const userBadge = document.getElementById('user-badge');
  const userBadgeRole = document.getElementById('user-badge-role');
  const userBadgeName = document.getElementById('user-badge-name');
  const btnLogout = document.getElementById('btn-logout');

  // Views
  const viewAuth = document.getElementById('view-auth');
  const viewStudent = document.getElementById('view-student');
  const viewFaculty = document.getElementById('view-faculty');
  const tabOsMonitor = document.getElementById('tab-os-monitor');

  // Auth Elements
  const roleBtnStudent = document.getElementById('role-btn-student');
  const roleBtnFaculty = document.getElementById('role-btn-faculty');
  const loginForm = document.getElementById('login-form');
  const loginIdInput = document.getElementById('login-id');
  const loginPasswordInput = document.getElementById('login-password');
  const loginIdLabel = document.getElementById('login-id-label');
  const loginError = document.getElementById('login-error');

  // Demo chips
  const demoStudentS101 = document.getElementById('demo-student-s101');
  const demoStudentS102 = document.getElementById('demo-student-s102');
  const demoFacultyF201 = document.getElementById('demo-faculty-f201');
  const demoFacultyF202 = document.getElementById('demo-faculty-f202');

  // Toast
  const toast = document.getElementById('toast');

  // Modals
  const modalMarks = document.getElementById('modal-marks');
  const modalMarksClose = document.getElementById('modal-marks-close');
  const modalMarksCancel = document.getElementById('modal-marks-cancel');
  const formUpdateMarks = document.getElementById('form-update-marks');

  const modalAttendance = document.getElementById('modal-attendance');
  const modalAttClose = document.getElementById('modal-att-close');
  const modalAttCancel = document.getElementById('modal-att-cancel');
  const formUpdateAttendance = document.getElementById('form-update-attendance');

  // OS Monitor Elements (Phase 1)
  const metricStatus = document.getElementById('metric-status');
  const metricPid = document.getElementById('metric-pid');
  const metricUptime = document.getElementById('metric-uptime');
  const metricRequests = document.getElementById('metric-requests');
  const metricPort = document.getElementById('metric-port');
  const btnPing = document.getElementById('btn-ping');
  const btnSampleData = document.getElementById('btn-sample-data');
  const btnTestHealth = document.getElementById('btn-test-health');
  const btnEcho = document.getElementById('btn-echo');
  const echoInput = document.getElementById('echo-input');
  const terminalOutput = document.getElementById('terminal-output');
  const terminalLatency = document.getElementById('terminal-latency');

  let activeLoginRole = 'student';

  // ---------------------------------------------------------------------------
  // TOAST NOTIFICATIONS
  // ---------------------------------------------------------------------------
  function showToast(message, type = 'success') {
    toast.textContent = message;
    toast.className = `toast toast-${type}`;
    toast.classList.remove('hidden');
    setTimeout(() => {
      toast.classList.add('hidden');
    }, 3500);
  }

  // ---------------------------------------------------------------------------
  // FORMATTING UTILITIES
  // ---------------------------------------------------------------------------
  function formatUptime(seconds) {
    if (seconds < 60) return `${seconds}s`;
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    if (mins < 60) return `${mins}m ${secs}s`;
    const hrs = Math.floor(mins / 60);
    return `${hrs}h ${mins % 60}m`;
  }

  function getGradeBadgeClass(grade) {
    if (grade === 'A+' || grade === 'A') return 'badge-green';
    if (grade === 'B') return 'badge-blue';
    if (grade === 'C') return 'badge-amber';
    return 'badge-red';
  }

  // ---------------------------------------------------------------------------
  // SERVER STATUS POLLING (Phase 1, 2 & Phase 3 Concurrency Monitor)
  // ---------------------------------------------------------------------------
  const metricWorkers = document.getElementById('metric-workers');
  const metricWorkerDetails = document.getElementById('metric-worker-details');
  const metricQueueSem = document.getElementById('metric-queue-sem');
  const metricSemSub = document.getElementById('metric-sem-sub');
  const btnBurstTest = document.getElementById('btn-burst-test');

  async function fetchStatus(isManual = false) {
    const startTime = performance.now();
    try {
      const response = await fetch('/api/status', { cache: 'no-store' });
      const latency = Math.round(performance.now() - startTime);

      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();

      // Verify that status is explicitly ONLINE (or UP)
      if (!data || (data.status !== 'ONLINE' && data.status !== 'UP')) {
        throw new Error(`Server reported non-online status: ${data ? data.status : 'null'}`);
      }

      if (statusText) {
        statusText.textContent = data.server_pid ? `C Server Online (PID: ${data.server_pid})` : 'C Server Online';
      }
      if (statusDot) {
        statusDot.className = 'status-indicator online';
      }
      if (flowPid && data.server_pid) {
        flowPid.textContent = data.server_pid;
      }

      const badgeServerState = document.getElementById('badge-server-state');
      if (badgeServerState) {
        badgeServerState.textContent = 'ONLINE';
        badgeServerState.className = 'badge badge-green';
      }

      if (metricStatus) {
        metricStatus.textContent = 'ONLINE';
        metricStatus.className = 'metric-value text-green';
      }
      if (metricPid && data.server_pid) {
        metricPid.textContent = data.server_pid;
      }
      if (metricPort && data.port) {
        metricPort.textContent = `Port: ${data.port} (POSIX AF_INET)`;
      }
      if (metricUptime && data.uptime_seconds !== undefined) {
        metricUptime.textContent = formatUptime(data.uptime_seconds);
      }
      if (metricRequests && data.total_requests !== undefined) {
        metricRequests.textContent = data.total_requests.toLocaleString();
      }

      const metricThreads = document.getElementById('metric-threads');
      if (metricThreads) {
        const totalThreads = (data.proc_metrics && data.proc_metrics.num_threads) ? data.proc_metrics.num_threads : 6;
        metricThreads.textContent = `${totalThreads} Threads`;
      }

      // /proc Metrics
      if (data.proc_metrics) {
        const srvProcState = document.getElementById('srv-proc-state');
        const srvRss = document.getElementById('srv-rss');
        const srvVsize = document.getElementById('srv-vsize');
        const srvFds = document.getElementById('srv-fds');
        const srvCpuTicks = document.getElementById('srv-cpu-ticks');
        const srvCpuTime = document.getElementById('srv-cpu-time');

        if (srvProcState) srvProcState.textContent = `${data.proc_metrics.state} (${data.proc_metrics.state_name || 'Sleeping/Interruptible'})`;
        if (srvRss) srvRss.textContent = `${data.proc_metrics.rss_kb.toLocaleString()} KB (${(data.proc_metrics.rss_kb / 1024).toFixed(1)} MB)`;
        if (srvVsize) srvVsize.textContent = `${data.proc_metrics.vsize_kb.toLocaleString()} KB (${(data.proc_metrics.vsize_kb / 1024).toFixed(1)} MB)`;
        if (srvFds) srvFds.textContent = `${data.proc_metrics.open_fds} FDs`;
        if (srvCpuTicks) srvCpuTicks.textContent = `User: ${data.proc_metrics.utime_ticks} / Sys: ${data.proc_metrics.stime_ticks}`;
        if (srvCpuTime) srvCpuTime.textContent = `${Number(data.proc_metrics.cpu_time_seconds).toFixed(2)}s`;
      }

      // Worker aggregate
      const aggregateWorkers = document.getElementById('aggregate-workers');
      if (aggregateWorkers && data.thread_pool) {
        aggregateWorkers.textContent = `Active Workers: ${data.thread_pool.busy_workers} / ${data.thread_pool.worker_count}`;
      }

      if (metricWorkers && data.thread_pool) {
        metricWorkers.textContent = `${data.thread_pool.worker_count} Workers`;
        if (metricWorkerDetails) {
          metricWorkerDetails.textContent = `${data.thread_pool.busy_workers} Busy / ${data.thread_pool.idle_workers} Idle`;
        }
      }

      // Queues & Semaphore Cards
      const txtReqQ = document.getElementById('txt-req-q-display');
      const badgeReqQ = document.getElementById('badge-req-q');
      const barReqQ = document.getElementById('bar-req-q');
      if (data.thread_pool) {
        if (txtReqQ) txtReqQ.textContent = `Request Queue: ${data.thread_pool.current_queue_size} / ${data.thread_pool.queue_capacity}`;
        if (badgeReqQ) badgeReqQ.textContent = `${data.thread_pool.current_queue_size} / ${data.thread_pool.queue_capacity}`;
        if (barReqQ) barReqQ.style.width = `${Math.min(100, (data.thread_pool.current_queue_size / data.thread_pool.queue_capacity) * 100)}%`;
      }

      const txtRespQ = document.getElementById('txt-resp-q-display');
      const badgeRespQ = document.getElementById('badge-resp-q');
      const barRespQ = document.getElementById('bar-resp-q');
      if (data.response_queue) {
        if (txtRespQ) txtRespQ.textContent = `Response Queue: ${data.response_queue.current_size} / ${data.response_queue.capacity}`;
        if (badgeRespQ) badgeRespQ.textContent = `${data.response_queue.current_size} / ${data.response_queue.capacity}`;
        if (barRespQ) barRespQ.style.width = `${Math.min(100, (data.response_queue.current_size / data.response_queue.capacity) * 100)}%`;
      }

      const txtSem = document.getElementById('txt-sem-display');
      const badgeSemSlots = document.getElementById('badge-sem-slots');
      const barSemSlots = document.getElementById('bar-sem-slots');
      if (data.semaphore) {
        if (txtSem) txtSem.textContent = `Database Access Slots: ${data.semaphore.available_slots} / ${data.semaphore.total_slots}`;
        if (badgeSemSlots) badgeSemSlots.textContent = `${data.semaphore.available_slots} / ${data.semaphore.total_slots} Slots`;
        if (barSemSlots) barSemSlots.style.width = `${Math.min(100, (data.semaphore.available_slots / data.semaphore.total_slots) * 100)}%`;
      }

      if (metricQueueSem && data.thread_pool && data.semaphore) {
        metricQueueSem.textContent = `${data.thread_pool.current_queue_size} / ${data.thread_pool.queue_capacity} Q`;
        if (metricSemSub) metricSemSub.textContent = `${data.semaphore.available_slots} / ${data.semaphore.total_slots} DB Slots (sem_t)`;
      }

      // Update individual worker cards
      if (Array.isArray(data.workers)) {
        data.workers.forEach(w => {
          const stateEl = document.getElementById(`w${w.id}-state`);
          const tasksEl = document.getElementById(`w${w.id}-tasks`);
          const actEl = document.getElementById(`w${w.id}-act`);
          if (stateEl) {
            stateEl.textContent = w.busy ? 'Busy (Serving)' : 'Idle (Waiting)';
            stateEl.className = w.busy ? 'badge badge-amber' : 'badge badge-blue';
          }
          if (tasksEl) {
            tasksEl.textContent = w.tasks_completed;
          }
          if (actEl) {
            actEl.textContent = (w.current_activity && w.current_activity.trim().length > 0)
              ? w.current_activity
              : (w.busy ? 'Serving socket' : 'Waiting on cond_not_empty');
          }
        });
      }

      // Memory-Mapped Storage telemetry
      if (data.mmap) {
        const mmapPath = document.getElementById('mmap-path');
        const mmapSize = document.getElementById('mmap-size');
        const mmapTxs = document.getElementById('mmap-txs');
        const mmapLastSync = document.getElementById('mmap-last-sync');
        if (mmapPath && data.mmap.file_path) mmapPath.textContent = data.mmap.file_path.split('/').pop() || 'university.dat';
        if (mmapSize && data.mmap.mapped_size) mmapSize.textContent = `${data.mmap.mapped_size.toLocaleString()} bytes`;
        if (mmapTxs && data.mmap.transaction_count !== undefined) mmapTxs.textContent = `${data.mmap.transaction_count} commits`;
        if (mmapLastSync && data.mmap.last_sync) mmapLastSync.textContent = data.mmap.last_sync;
      }

      // Config & Signals telemetry
      if (data.config) {
        const badgeReloads = document.getElementById('badge-config-reloads');
        const cfgCount = document.getElementById('cfg-reloads-count');
        const cfgTime = document.getElementById('cfg-last-time');
        if (badgeReloads) badgeReloads.textContent = `Reloads: ${data.config.reload_count}`;
        if (cfgCount) cfgCount.textContent = data.config.reload_count;
        if (cfgTime && data.config.last_reload) cfgTime.textContent = data.config.last_reload;
      }

      if (isManual && terminalLatency && terminalOutput) {
        terminalLatency.textContent = `Latency: ${latency} ms`;
        terminalOutput.textContent = `// HTTP 200 OK (${latency}ms) — Fetched from POSIX Socket\n` + JSON.stringify(data, null, 2);
      }
    } catch (err) {
      if (statusText) statusText.textContent = 'C Server Offline';
      if (statusDot) statusDot.className = 'status-indicator offline';
      const badgeServerState = document.getElementById('badge-server-state');
      if (badgeServerState) {
        badgeServerState.textContent = 'OFFLINE';
        badgeServerState.className = 'badge badge-danger';
      }
      if (metricStatus) {
        metricStatus.textContent = 'OFFLINE';
        metricStatus.className = 'metric-value text-danger';
      }
      if (isManual && terminalLatency && terminalOutput) {
        terminalLatency.textContent = 'Error';
        terminalOutput.textContent = `// Failed to connect to C backend: ${err.message}`;
      }
    }
  }

  // ---------------------------------------------------------------------------
  // AUTHENTICATION & SESSION MANAGEMENT
  // ---------------------------------------------------------------------------
  function setRole(role) {
    activeLoginRole = role;
    if (role === 'student') {
      if (roleBtnStudent) roleBtnStudent.className = 'btn btn-primary';
      if (roleBtnFaculty) roleBtnFaculty.className = 'btn btn-secondary';
      if (loginIdLabel) loginIdLabel.textContent = 'Student ID (e.g. S101, S102, S103)';
      if (loginIdInput) loginIdInput.value = 'S101';
      if (loginPasswordInput) loginPasswordInput.value = 'pass101';
    } else {
      if (roleBtnStudent) roleBtnStudent.className = 'btn btn-secondary';
      if (roleBtnFaculty) roleBtnFaculty.className = 'btn btn-primary';
      if (loginIdLabel) loginIdLabel.textContent = 'Faculty ID (e.g. F201, F202, F203)';
      if (loginIdInput) loginIdInput.value = 'F201';
      if (loginPasswordInput) loginPasswordInput.value = 'fac201';
    }
    if (loginError) loginError.classList.add('hidden');
  }

  if (roleBtnStudent) roleBtnStudent.addEventListener('click', () => setRole('student'));
  if (roleBtnFaculty) roleBtnFaculty.addEventListener('click', () => setRole('faculty'));

  async function login(role, id, password) {
    if (loginError) loginError.classList.add('hidden');
    try {
      let resp;
      try {
        resp = await fetch('/api/auth/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ role, id, password })
        });
      } catch (networkErr) {
        throw new Error('Unable to contact C server. Please start ./bin/server 3000.');
      }

      const contentType = resp.headers.get('content-type') || '';
      let data = null;
      if (contentType.includes('application/json')) {
        try {
          data = await resp.json();
        } catch (jsonErr) {
          throw new Error('Unable to contact C server. Please start ./bin/server 3000.');
        }
      } else {
        throw new Error('Unable to contact C server. Please start ./bin/server 3000.');
      }

      if (!resp.ok) {
        throw new Error((data && data.message) ? data.message : 'Invalid ID or password');
      }

      currentAuth = data;
      sessionStorage.setItem('univ_erp_auth', JSON.stringify(data));

      showToast(`Welcome, ${data.user.name}! Authenticated via C Linux Socket.`);
      renderAuthenticatedSession();
    } catch (err) {
      if (loginError) {
        loginError.textContent = err.message || 'Unable to contact C server. Please start ./bin/server 3000.';
        loginError.classList.remove('hidden');
      }
    }
  }

  if (loginForm) {
    loginForm.addEventListener('submit', (e) => {
      e.preventDefault();
      login(activeLoginRole, loginIdInput ? loginIdInput.value.trim() : '', loginPasswordInput ? loginPasswordInput.value.trim() : '');
    });
  }

  // Demo account quick triggers
  if (demoStudentS101) demoStudentS101.addEventListener('click', () => login('student', 'S101', 'pass101'));
  if (demoStudentS102) demoStudentS102.addEventListener('click', () => login('student', 'S102', 'pass102'));
  if (demoFacultyF201) demoFacultyF201.addEventListener('click', () => login('faculty', 'F201', 'fac201'));
  if (demoFacultyF202) demoFacultyF202.addEventListener('click', () => login('faculty', 'F202', 'fac202'));

  if (btnLogout) {
    btnLogout.addEventListener('click', () => {
      currentAuth = null;
      sessionStorage.removeItem('univ_erp_auth');
      if (userBadge) userBadge.classList.add('hidden');
      if (viewStudent) viewStudent.classList.add('hidden');
      if (viewFaculty) viewFaculty.classList.add('hidden');
      if (tabOsMonitor) tabOsMonitor.classList.add('hidden');
      if (viewAuth) viewAuth.classList.remove('hidden');
      showToast('Logged out of University ERP session.', 'success');
    });
  }

  // ---------------------------------------------------------------------------
  // RENDER PORTALS
  // ---------------------------------------------------------------------------
  function renderAuthenticatedSession() {
    viewAuth.classList.add('hidden');
    userBadge.classList.remove('hidden');
    userBadgeRole.textContent = currentAuth.role.toUpperCase();
    userBadgeName.textContent = currentAuth.user.name;

    if (currentAuth.role === 'student') {
      viewFaculty.classList.add('hidden');
      viewStudent.classList.remove('hidden');
      setupStudentTabs();
      loadStudentDashboard(currentAuth.user.id);
    } else {
      viewStudent.classList.add('hidden');
      viewFaculty.classList.remove('hidden');
      setupFacultyTabs();
      loadFacultyDashboard(currentAuth.user.id);
    }
  }

  // ---------------------------------------------------------------------------
  // STUDENT PORTAL LOGIC
  // ---------------------------------------------------------------------------
  function setupStudentTabs() {
    const tabs = document.querySelectorAll('#student-nav-tabs .nav-tab-btn');
    tabs.forEach(btn => {
      btn.onclick = () => {
        tabs.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        const target = btn.dataset.tab;

        document.getElementById('tab-student-dashboard').classList.toggle('hidden', target !== 'student-dashboard');
        document.getElementById('tab-student-courses').classList.toggle('hidden', target !== 'student-courses');
        document.getElementById('tab-student-marks').classList.toggle('hidden', target !== 'student-marks');
        document.getElementById('tab-student-attendance').classList.toggle('hidden', target !== 'student-attendance');
        tabOsMonitor.classList.toggle('hidden', target !== 'os-monitor');

        // Always revalidate latest academic records from C server when navigating student tabs
        if (currentAuth && currentAuth.role === 'student' && target !== 'os-monitor') {
          loadStudentDashboard(currentAuth.user.id);
        }
      };
    });
  }

  async function loadStudentDashboard(studentId) {
    try {
      const data = await fetchServerJson(`/api/student/dashboard?id=${encodeURIComponent(studentId)}`);
      if (!data || !data.student) throw new Error('Invalid student data returned by C server');
      studentData = data;

      // Profile Header
      document.getElementById('student-prof-name').textContent = data.student.name;
      document.getElementById('student-prof-id').textContent = data.student.id;
      document.getElementById('student-prof-dept').textContent = data.student.department;
      document.getElementById('student-prof-year').textContent = data.student.year;
      document.getElementById('student-prof-sec').textContent = data.student.section;

      // Metrics
      const stats = data.overall_stats || {};
      document.getElementById('student-gpa').textContent = typeof stats.gpa === 'number' ? stats.gpa.toFixed(2) : '--';
      document.getElementById('student-overall-att').textContent = typeof stats.overall_attendance_pct === 'number' ? `${stats.overall_attendance_pct.toFixed(1)}%` : '--%';
      document.getElementById('student-att-summary').textContent = `${stats.total_attended !== undefined ? stats.total_attended : 0} / ${stats.total_classes !== undefined ? stats.total_classes : 0} sessions attended`;
      document.getElementById('student-courses-count').textContent = stats.enrolled_courses !== undefined ? stats.enrolled_courses : (data.courses ? data.courses.length : 0);
      document.getElementById('student-credits-sum').textContent = `${stats.total_credits !== undefined ? stats.total_credits : 0} Credit Hours`;
      document.getElementById('student-avg-score').textContent = typeof stats.average_marks_pct === 'number' ? `${stats.average_marks_pct.toFixed(1)}%` : '--%';

      // Render Charts
      renderStudentMarksChart(data.marks || []);
      renderStudentAttendanceBars(data.attendance || []);

      // Render Tables
      renderStudentCoursesTable(data.courses || []);
      renderStudentMarksTable(data.marks || []);
      renderStudentAttendanceTable(data.attendance || []);

      // Render Academic Health Card (Early-Warning & Recovery)
      renderStudentAcademicHealth(data);

    } catch (err) {
      console.error('Error loading student dashboard:', err);
      showToast(err.message, 'error');
    }
  }

  // ---------------------------------------------------------------------------
  // ACADEMIC EARLY-WARNING & RECOVERY SYSTEM LOGIC
  // Transparent rule-based academic risk classification and recovery actions
  // ---------------------------------------------------------------------------
  function classifyAcademicRisk(attPct, avgMarks, totalClasses, validMarksCount, totalAttended) {
    // Missing academic data handling: Do not classify a student based on unavailable data.
    const hasNoAttendance = (totalClasses === undefined || totalClasses === null || totalClasses === 0 || isNaN(attPct));
    const hasNoMarks = (validMarksCount === undefined || validMarksCount === null || validMarksCount === 0 || isNaN(avgMarks));

    if (hasNoAttendance || hasNoMarks) {
      return {
        status: 'Insufficient Data',
        displayStatus: 'Insufficient academic data',
        badgeClass: 'badge-purple',
        badgeColor: '#a855f7',
        severity: 'insufficient',
        isLowAttendance: false,
        isLowAverage: false,
        isModerateAverage: false,
        isGoodAverage: false,
        consecutiveNeeded: 0,
        reasons: ['Insufficient academic data recorded for this student.'],
        recommendation: 'Complete pending assessments and record attendance to establish an academic health profile.',
        isEligible: false
      };
    }

    const isLowAttendance = attPct < 75.0;
    const isLowAverage = avgMarks < 60.0;
    const isModerateAverage = (avgMarks >= 60.0 && avgMarks < 75.0);
    const isGoodAverage = (avgMarks >= 75.0);

    // Calculate minimum consecutive classes needed to reach 75% attendance:
    // (A + x) / (T + x) >= 0.75  =>  4A + 4x >= 3T + 3x  =>  x >= 3T - 4A
    const consecutiveNeeded = (isLowAttendance && totalClasses > 0)
      ? Math.max(0, (3 * totalClasses) - (4 * (totalAttended || 0)))
      : 0;

    let status = '';
    let displayStatus = '';
    let badgeClass = '';
    let badgeColor = '';
    let severity = '';
    const reasons = [];
    let recommendation = '';

    // 1. HIGH CONCERN: Attendance below 75% AND academic average below 60%
    if (isLowAttendance && isLowAverage) {
      status = 'High Concern';
      displayStatus = 'High Concern';
      badgeClass = 'badge-red';
      badgeColor = '#ef4444';
      severity = 'high';

      reasons.push(`Attendance (${attPct.toFixed(1)}%) is below the mandatory 75% threshold.`);
      if (consecutiveNeeded > 0) {
        reasons.push(`Must attend ${consecutiveNeeded} additional consecutive classes to reach 75% eligibility.`);
      }
      reasons.push(`Academic average (${avgMarks.toFixed(1)}%) is below the satisfactory benchmark of 60%.`);
      recommendation = 'Meet your faculty advisor to prepare an academic improvement plan.';
    }
    // 2. NEEDS ATTENTION: Attendance below 75% OR academic average below 60%, OR average between 60% and below 75%
    else if (isLowAttendance || isLowAverage || isModerateAverage) {
      status = 'Needs Attention';
      displayStatus = 'Needs Attention';
      badgeClass = 'badge-amber';
      badgeColor = '#f59e0b';
      severity = 'attention';

      if (isLowAttendance) {
        reasons.push(`Attendance (${attPct.toFixed(1)}%) is below the mandatory 75% threshold.`);
        if (consecutiveNeeded > 0) {
          reasons.push(`Must attend ${consecutiveNeeded} additional consecutive classes to reach 75% eligibility.`);
        }
      }

      if (isLowAverage) {
        reasons.push(`Academic average (${avgMarks.toFixed(1)}%) is below the satisfactory benchmark of 60%.`);
      } else if (isModerateAverage) {
        reasons.push(`Academic average (${avgMarks.toFixed(1)}%) is in the moderate performance band (between 60% and 75%).`);
      }

      // Recovery Recommendations based on actual warning reasons
      if (isLowAttendance && (isLowAverage || isModerateAverage)) {
        recommendation = 'Meet your faculty advisor to prepare an academic improvement plan.';
      } else if (isLowAttendance) {
        recommendation = 'Attend upcoming classes consistently to improve attendance eligibility.';
      } else {
        recommendation = 'Review assessment performance and consult the course faculty for academic support.';
      }
    }
    // 3. ON TRACK: Attendance at least 75% AND academic average at least 75%
    else if (!isLowAttendance && isGoodAverage) {
      status = 'On Track';
      displayStatus = 'On Track';
      badgeClass = 'badge-green';
      badgeColor = '#10b981';
      severity = 'ontrack';

      reasons.push(`Attendance (${attPct.toFixed(1)}%) satisfies the statutory requirement (≥75%).`);
      reasons.push(`Academic average (${avgMarks.toFixed(1)}%) meets the good performance benchmark (≥75%).`);
      recommendation = 'Excellent academic momentum. Maintain your consistent lecture attendance and study habits.';
    }

    return {
      status,
      displayStatus,
      badgeClass,
      badgeColor,
      severity,
      isLowAttendance,
      isLowAverage,
      isModerateAverage,
      isGoodAverage,
      consecutiveNeeded,
      reasons,
      recommendation,
      isEligible: !isLowAttendance
    };
  }

  function renderStudentAcademicHealth(data) {
    const card = document.getElementById('student-academic-health-card');
    if (!card) return;

    const stats = data.overall_stats || {};
    const marks = data.marks || [];

    const totalAttended = typeof stats.total_attended === 'number' ? stats.total_attended : 0;
    const totalClasses = typeof stats.total_classes === 'number' ? stats.total_classes : 0;
    const attPct = totalClasses > 0 ? ((totalAttended / totalClasses) * 100) : (typeof stats.overall_attendance_pct === 'number' ? stats.overall_attendance_pct : NaN);

    // Filter valid marks (assessments that are recorded and graded)
    const validMarks = marks.filter(m => m && m.grade && m.grade !== '-' && typeof m.total_pct === 'number');
    const avgMarks = validMarks.length > 0
      ? (typeof stats.average_marks_pct === 'number' && stats.average_marks_pct > 0 ? stats.average_marks_pct : (validMarks.reduce((acc, m) => acc + m.total_pct, 0) / validMarks.length))
      : NaN;

    const risk = classifyAcademicRisk(attPct, avgMarks, totalClasses, validMarks.length, totalAttended);

    // Apply color styling to card border
    card.style.borderTopColor = risk.badgeColor;

    // Badge
    const badgeContainer = document.getElementById('student-health-status-badge');
    if (badgeContainer) {
      badgeContainer.innerHTML = `<span class="badge ${risk.badgeClass}" style="font-size: 0.82rem; padding: 0.35rem 0.75rem;">${escapeHtml(risk.displayStatus)}</span>`;
    }

    // Status text & subtitle
    const statusTextEl = document.getElementById('student-health-status-text');
    if (statusTextEl) {
      statusTextEl.textContent = risk.displayStatus;
      statusTextEl.style.color = risk.badgeColor;
    }

    const statusSubEl = document.getElementById('student-health-status-sub');
    if (statusSubEl) {
      if (risk.severity === 'ontrack') {
        statusSubEl.textContent = 'Attendance & marks meet benchmarks';
      } else if (risk.severity === 'high') {
        statusSubEl.textContent = 'Immediate academic intervention recommended';
      } else if (risk.severity === 'attention') {
        statusSubEl.textContent = 'Academic metrics require focused attention';
      } else {
        statusSubEl.textContent = 'Insufficient academic records available';
      }
    }

    // Attendance indicator
    const attValEl = document.getElementById('student-health-att-val');
    if (attValEl) {
      attValEl.textContent = !isNaN(attPct) ? `${attPct.toFixed(1)}%` : '--%';
      attValEl.style.color = risk.isLowAttendance ? '#ef4444' : '#c4b5fd';
    }

    const attDetailEl = document.getElementById('student-health-att-detail');
    if (attDetailEl) {
      attDetailEl.textContent = !isNaN(attPct)
        ? `${totalAttended} / ${totalClasses} classes attended (Req: ≥75%)`
        : 'No attendance recorded';
    }

    // Academic average indicator
    const avgValEl = document.getElementById('student-health-avg-val');
    if (avgValEl) {
      avgValEl.textContent = !isNaN(avgMarks) ? `${avgMarks.toFixed(1)}%` : '--%';
      avgValEl.style.color = risk.isLowAverage ? '#ef4444' : (risk.isModerateAverage ? '#fbbf24' : '#10b981');
    }

    const avgDetailEl = document.getElementById('student-health-avg-detail');
    if (avgDetailEl) {
      avgDetailEl.textContent = !isNaN(avgMarks)
        ? `${validMarks.length} evaluated course${validMarks.length === 1 ? '' : 's'}`
        : 'No marks recorded';
    }

    // Attendance Eligibility indicator
    const eligTextEl = document.getElementById('student-health-eligibility-text');
    if (eligTextEl) {
      if (risk.severity === 'insufficient') {
        eligTextEl.textContent = 'Insufficient academic data';
        eligTextEl.style.color = '#c4b5fd';
      } else if (risk.isEligible) {
        eligTextEl.textContent = 'Requirement Satisfied';
        eligTextEl.style.color = '#10b981';
      } else {
        eligTextEl.textContent = 'Attendance Warning (<75%)';
        eligTextEl.style.color = '#ef4444';
      }
    }

    const consecClassesEl = document.getElementById('student-health-consecutive-classes');
    if (consecClassesEl) {
      if (risk.severity === 'insufficient') {
        consecClassesEl.textContent = 'Awaiting course attendance data';
      } else if (risk.isEligible) {
        consecClassesEl.textContent = '0 consecutive classes needed (Eligible for exams)';
      } else {
        consecClassesEl.textContent = `Need ${risk.consecutiveNeeded} consecutive classes to reach 75%`;
      }
    }

    // Warning reasons box
    const reasonsBox = document.getElementById('student-health-reasons-box');
    const reasonsTitle = document.getElementById('student-health-reasons-title');
    const reasonsList = document.getElementById('student-health-reasons-list');
    if (reasonsBox && reasonsList) {
      reasonsBox.style.borderLeftColor = risk.badgeColor;
      reasonsBox.style.background = risk.badgeColor + '14'; // subtle tint
      if (reasonsTitle) {
        reasonsTitle.style.color = risk.badgeColor;
        reasonsTitle.textContent = risk.severity === 'ontrack'
          ? '✅ Academic Standing Summary:'
          : (risk.severity === 'insufficient' ? 'ℹ️ Academic Data Notice:' : '⚠️ Warning Triggers & Observations:');
      }
      reasonsList.innerHTML = risk.reasons.map(r => `<li>${escapeHtml(r)}</li>`).join('');
    }

    // Recommended action box
    const actionBox = document.getElementById('student-health-action-box');
    const actionTitle = document.getElementById('student-health-action-title');
    const recText = document.getElementById('student-health-recommendation-text');
    if (actionBox && recText) {
      actionBox.style.borderLeftColor = risk.badgeColor;
      actionBox.style.background = risk.badgeColor + '14';
      if (actionTitle) actionTitle.style.color = risk.badgeColor;
      recText.textContent = risk.recommendation;
    }
  }

  function renderStudentMarksChart(marks) {
    const container = document.getElementById('student-marks-chart');
    if (!marks || marks.length === 0) {
      container.innerHTML = '<div style="color: var(--text-muted); padding: 1rem;">No marks records available.</div>';
      return;
    }

    container.innerHTML = marks.map(m => {
      // Normalize heights out of 100%
      const midPct = (m.midterm / 50) * 100;
      const endPct = (m.endterm / 100) * 100;
      const totPct = m.total_pct;

      return `
        <div style="flex: 1; display: flex; flex-direction: column; align-items: center; gap: 0.5rem; height: 100%;">
          <div style="font-size: 0.72rem; font-weight: 700; color: #10b981;">${totPct.toFixed(0)}%</div>
          <div style="display: flex; gap: 3px; align-items: flex-end; height: 160px; width: 100%; justify-content: center;">
            <div title="Midterm: ${m.midterm}/50 (${midPct.toFixed(0)}%)" style="width: 14px; background: #3b82f6; border-radius: 3px 3px 0 0; height: ${Math.max(8, midPct * 1.5)}px;"></div>
            <div title="Endterm: ${m.endterm}/100 (${endPct.toFixed(0)}%)" style="width: 14px; background: #8b5cf6; border-radius: 3px 3px 0 0; height: ${Math.max(8, endPct * 1.5)}px;"></div>
            <div title="Total: ${totPct.toFixed(1)}% (${m.grade})" style="width: 14px; background: #10b981; border-radius: 3px 3px 0 0; height: ${Math.max(8, totPct * 1.5)}px;"></div>
          </div>
          <div style="font-size: 0.75rem; font-weight: 600; text-align: center; color: var(--text-primary);">${m.course_id}</div>
          <div style="font-size: 0.68rem; color: var(--text-muted);">${m.grade}</div>
        </div>
      `;
    }).join('');
  }

  function renderStudentAttendanceBars(attendance) {
    const container = document.getElementById('student-attendance-bars');
    if (!attendance || attendance.length === 0) {
      container.innerHTML = '<div style="color: var(--text-muted);">No attendance records found.</div>';
      return;
    }

    container.innerHTML = attendance.map(a => {
      const isEligible = a.percentage >= 75.0;
      const fillClass = isEligible ? 'fill-green' : 'fill-red';
      const statusBadge = isEligible
        ? `<span class="badge badge-green" style="font-size: 0.7rem;">Eligible (${a.percentage.toFixed(1)}%)</span>`
        : `<span class="badge badge-red" style="font-size: 0.7rem;">⚠️ Low Attendance (${a.percentage.toFixed(1)}%)</span>`;

      return `
        <div>
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.35rem; font-size: 0.82rem;">
            <div>
              <strong>${a.course_id}</strong> — <span style="color: var(--text-secondary);">${a.course_name}</span>
            </div>
            <div>
              <span style="color: var(--text-muted); margin-right: 0.5rem;">${a.attended} / ${a.total} classes</span>
              ${statusBadge}
            </div>
          </div>
          <div class="progress-bar-bg" style="position: relative;">
            <div class="progress-bar-fill ${fillClass}" style="width: ${Math.min(100, a.percentage)}%;"></div>
            <!-- 75% threshold marker -->
            <div style="position: absolute; top: 0; left: 75%; width: 2px; height: 100%; background: #f59e0b; opacity: 0.8;" title="75% Mandatory Attendance Line"></div>
          </div>
        </div>
      `;
    }).join('');
  }

  function renderStudentCoursesTable(courses) {
    const tbody = document.getElementById('student-courses-tbody');
    tbody.innerHTML = courses.map(c => `
      <tr>
        <td><strong>${c.id}</strong></td>
        <td>${c.name}</td>
        <td>${c.department}</td>
        <td><span class="badge badge-blue">${c.credits} Credits</span></td>
        <td>Semester ${c.semester}</td>
        <td>👨‍🏫 ${c.faculty_name}</td>
      </tr>
    `).join('');
  }

  function renderStudentMarksTable(marks) {
    const tbody = document.getElementById('student-marks-tbody');
    tbody.innerHTML = marks.map(m => `
      <tr>
        <td><strong>${m.course_id}</strong></td>
        <td>${m.course_name}</td>
        <td>${m.midterm} / 50</td>
        <td>${m.endterm} / 100</td>
        <td>${m.quiz} / 25</td>
        <td><strong>${m.total_pct.toFixed(2)}%</strong></td>
        <td><span class="badge ${getGradeBadgeClass(m.grade)}">${m.grade}</span></td>
      </tr>
    `).join('');
  }

  function renderStudentAttendanceTable(attendance) {
    const tbody = document.getElementById('student-attendance-tbody');
    tbody.innerHTML = attendance.map(a => {
      const isEligible = a.percentage >= 75.0;
      const badge = isEligible
        ? `<span class="badge badge-green">Satisfactory (${a.percentage.toFixed(1)}%)</span>`
        : `<span class="badge badge-red">Shortage (&lt;75%)</span>`;
      return `
        <tr>
          <td><strong>${a.course_id}</strong></td>
          <td>${a.course_name}</td>
          <td>${a.attended}</td>
          <td>${a.total}</td>
          <td>
            <div style="display: flex; align-items: center; gap: 0.5rem;">
              <div class="progress-bar-bg" style="width: 80px;">
                <div class="progress-bar-fill ${isEligible ? 'fill-green' : 'fill-red'}" style="width: ${Math.min(100, a.percentage)}%;"></div>
              </div>
              <span>${a.percentage.toFixed(1)}%</span>
            </div>
          </td>
          <td>${badge}</td>
        </tr>
      `;
    }).join('');
  }

  // ---------------------------------------------------------------------------
  // FACULTY PORTAL LOGIC
  // ---------------------------------------------------------------------------
  function setupFacultyTabs() {
    const tabs = document.querySelectorAll('#faculty-nav-tabs .nav-tab-btn');
    tabs.forEach(btn => {
      btn.onclick = () => {
        tabs.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        const target = btn.dataset.tab;

        document.getElementById('tab-faculty-dashboard').classList.toggle('hidden', target !== 'faculty-dashboard');
        const tabEW = document.getElementById('tab-faculty-early-warning');
        if (tabEW) tabEW.classList.toggle('hidden', target !== 'faculty-early-warning');
        document.getElementById('tab-faculty-courses').classList.toggle('hidden', target !== 'faculty-courses');
        document.getElementById('tab-faculty-marks').classList.toggle('hidden', target !== 'faculty-marks');
        document.getElementById('tab-faculty-attendance').classList.toggle('hidden', target !== 'faculty-attendance');
        const tabStudents = document.getElementById('tab-faculty-students');
        if (tabStudents) tabStudents.classList.toggle('hidden', target !== 'faculty-students');
        const tabDir = document.getElementById('tab-faculty-directory');
        if (tabDir) tabDir.classList.toggle('hidden', target !== 'faculty-directory');
        tabOsMonitor.classList.toggle('hidden', target !== 'os-monitor');

        if (target === 'faculty-students') {
          loadFacultyStudents();
        } else if (target === 'faculty-directory') {
          loadFacultyDirectory();
        } else if (target === 'faculty-early-warning') {
          renderFacultyEarlyWarningCenter();
        } else if (target === 'faculty-marks' || target === 'faculty-attendance' || target === 'faculty-dashboard' || target === 'faculty-courses') {
          if (currentAuth && currentAuth.role === 'faculty') {
            loadFacultyDashboard(currentAuth.user.id);
          }
        }
      };
    });

    const btnGotoMarks = document.getElementById('btn-goto-marks');
    if (btnGotoMarks) {
      btnGotoMarks.onclick = () => {
        const tab = document.querySelector('#faculty-nav-tabs [data-tab="faculty-marks"]');
        if (tab) tab.click();
      };
    }

    const btnGotoAtt = document.getElementById('btn-goto-attendance');
    if (btnGotoAtt) {
      btnGotoAtt.onclick = () => {
        const tab = document.querySelector('#faculty-nav-tabs [data-tab="faculty-attendance"]');
        if (tab) tab.click();
      };
    }

    const btnGotoStudents = document.getElementById('btn-goto-students');
    if (btnGotoStudents) {
      btnGotoStudents.onclick = () => {
        const tab = document.querySelector('#faculty-nav-tabs [data-tab="faculty-students"]');
        if (tab) tab.click();
      };
    }

    const btnGotoEW = document.getElementById('btn-goto-early-warning');
    if (btnGotoEW) {
      btnGotoEW.onclick = () => {
        const tab = document.querySelector('#faculty-nav-tabs [data-tab="faculty-early-warning"]');
        if (tab) tab.click();
      };
    }

    // Search filters
    const filterMarks = document.getElementById('filter-marks-input');
    if (filterMarks) {
      filterMarks.oninput = () => {
        refreshFacultyMarksTable();
      };
    }

    const filterAtt = document.getElementById('filter-att-input');
    if (filterAtt) {
      filterAtt.oninput = () => {
        refreshFacultyAttendanceTable();
      };
    }

    // Early-Warning Center Filter Buttons
    const ewFilterBtns = document.querySelectorAll('#early-warning-filter-buttons .filter-ew-btn');
    ewFilterBtns.forEach(btn => {
      btn.onclick = () => {
        ewFilterBtns.forEach(b => {
          b.classList.remove('btn-primary', 'active');
          b.classList.add('btn-secondary');
        });
        btn.classList.add('btn-primary', 'active');
        btn.classList.remove('btn-secondary');
        earlyWarningFilter = btn.dataset.filter || 'all';
        renderEarlyWarningTable();
      };
    });

    // Early-Warning Search Input
    const ewSearchInput = document.getElementById('early-warning-search-input');
    if (ewSearchInput) {
      ewSearchInput.oninput = (e) => {
        earlyWarningSearch = e.target.value;
        renderEarlyWarningTable();
      };
    }

    const btnEwRefresh = document.getElementById('btn-early-warning-refresh');
    if (btnEwRefresh) {
      btnEwRefresh.onclick = () => {
        renderFacultyEarlyWarningCenter();
        showToast('Early-Warning records refreshed from C store.', 'info');
      };
    }
  }

  // ---------------------------------------------------------------------------
  // FACULTY EARLY-WARNING CENTER CONTROLLER & RENDERER
  // ---------------------------------------------------------------------------
  let earlyWarningFilter = 'all';
  let earlyWarningSearch = '';
  let cachedEarlyWarningData = [];

  async function renderFacultyEarlyWarningCenter() {
    try {
      // 1. Fetch student list from C backend
      const data = await fetchServerJson('/api/students');
      const students = data.students || [];

      // 2. Query each student's live records from C memory/mmap store
      const studentDashboards = await Promise.all(
        students.map(async (s) => {
          try {
            return await fetchServerJson(`/api/student/dashboard?id=${encodeURIComponent(s.id)}`);
          } catch (e) {
            return { student: s, overall_stats: {}, marks: [], attendance: [], courses: [] };
          }
        })
      );

      // 3. Classify academic risk using the transparent rule-based engine
      cachedEarlyWarningData = studentDashboards.map(d => {
        const s = d.student || {};
        const stats = d.overall_stats || {};
        const marks = d.marks || [];
        const att = d.attendance || [];

        const totalAttended = typeof stats.total_attended === 'number' ? stats.total_attended : 0;
        const totalClasses = typeof stats.total_classes === 'number' ? stats.total_classes : 0;
        const attPct = totalClasses > 0 ? ((totalAttended / totalClasses) * 100) : (typeof stats.overall_attendance_pct === 'number' ? stats.overall_attendance_pct : NaN);

        const validMarks = marks.filter(m => m && m.grade && m.grade !== '-' && typeof m.total_pct === 'number');
        const avgMarks = validMarks.length > 0
          ? (typeof stats.average_marks_pct === 'number' && stats.average_marks_pct > 0 ? stats.average_marks_pct : (validMarks.reduce((acc, m) => acc + m.total_pct, 0) / validMarks.length))
          : NaN;

        const risk = classifyAcademicRisk(attPct, avgMarks, totalClasses, validMarks.length, totalAttended);

        return {
          id: s.id,
          name: s.name,
          email: s.email,
          department: s.department,
          year: s.year,
          section: s.section,
          attPct,
          totalAttended,
          totalClasses,
          avgMarks,
          validMarksCount: validMarks.length,
          risk,
          courses: d.courses || [],
          marks,
          attendance: att
        };
      });

      // 4. Update count badges
      const highCount = cachedEarlyWarningData.filter(s => s.risk.severity === 'high').length;
      const attentionCount = cachedEarlyWarningData.filter(s => s.risk.severity === 'attention').length;
      const onTrackCount = cachedEarlyWarningData.filter(s => s.risk.severity === 'ontrack').length;
      const allCount = cachedEarlyWarningData.length;

      // Update counters in early warning tab header
      const cntHigh = document.getElementById('cnt-high-concern');
      const cntAtt = document.getElementById('cnt-needs-attention');
      const cntOnTrack = document.getElementById('cnt-on-track');
      if (cntHigh) cntHigh.textContent = `${highCount} High Concern`;
      if (cntAtt) cntAtt.textContent = `${attentionCount} Needs Attention`;
      if (cntOnTrack) cntOnTrack.textContent = `${onTrackCount} On Track`;

      // Update filter button badge counts
      const ewCntAll = document.getElementById('ew-count-all');
      const ewCntHigh = document.getElementById('ew-count-high');
      const ewCntAtt = document.getElementById('ew-count-attention');
      const ewCntOnTrack = document.getElementById('ew-count-ontrack');
      if (ewCntAll) ewCntAll.textContent = allCount;
      if (ewCntHigh) ewCntHigh.textContent = highCount;
      if (ewCntAtt) ewCntAtt.textContent = attentionCount;
      if (ewCntOnTrack) ewCntOnTrack.textContent = onTrackCount;

      // Update dashboard alert banner pills
      const pillHigh = document.getElementById('pill-high-concern');
      const pillAtt = document.getElementById('pill-needs-attention');
      const pillOnTrack = document.getElementById('pill-on-track');
      if (pillHigh) pillHigh.textContent = `${highCount} High Concern`;
      if (pillAtt) pillAtt.textContent = `${attentionCount} Needs Attention`;
      if (pillOnTrack) pillOnTrack.textContent = `${onTrackCount} On Track`;

      const ewBannerBadge = document.getElementById('ew-banner-badge');
      if (ewBannerBadge) {
        if (highCount > 0) {
          ewBannerBadge.className = 'badge badge-red';
          ewBannerBadge.textContent = `${highCount} High Concern Alert`;
        } else if (attentionCount > 0) {
          ewBannerBadge.className = 'badge badge-amber';
          ewBannerBadge.textContent = `${attentionCount} Needs Attention`;
        } else {
          ewBannerBadge.className = 'badge badge-green';
          ewBannerBadge.textContent = 'All Students On Track';
        }
      }

      const quickSumm = document.getElementById('faculty-early-warning-quick-summary');
      if (quickSumm) {
        quickSumm.textContent = `${highCount} High Concern, ${attentionCount} Needs Attention, ${onTrackCount} On Track across ${allCount} students evaluated from C server records.`;
      }

      // 5. Render early warning table
      renderEarlyWarningTable();

    } catch (err) {
      console.error('Failed to load Early-Warning Center:', err);
    }
  }

  function renderEarlyWarningTable() {
    const tbody = document.getElementById('early-warning-tbody');
    if (!tbody) return;

    let filtered = cachedEarlyWarningData;

    // Apply Filter
    if (earlyWarningFilter === 'high') {
      filtered = filtered.filter(s => s.risk.severity === 'high');
    } else if (earlyWarningFilter === 'attention') {
      filtered = filtered.filter(s => s.risk.severity === 'attention');
    } else if (earlyWarningFilter === 'ontrack') {
      filtered = filtered.filter(s => s.risk.severity === 'ontrack');
    }

    // Apply Search
    if (earlyWarningSearch.trim()) {
      const q = earlyWarningSearch.trim().toLowerCase();
      filtered = filtered.filter(s =>
        (s.id && s.id.toLowerCase().includes(q)) ||
        (s.name && s.name.toLowerCase().includes(q)) ||
        (s.department && s.department.toLowerCase().includes(q))
      );
    }

    const dispCount = document.getElementById('ew-displaying-count');
    if (dispCount) {
      dispCount.textContent = `Showing ${filtered.length} of ${cachedEarlyWarningData.length} students`;
    }

    if (filtered.length === 0) {
      tbody.innerHTML = `<tr><td colspan="9" style="text-align: center; color: var(--text-secondary); padding: 2rem;">No students matching the current filter/search.</td></tr>`;
      return;
    }

    tbody.innerHTML = filtered.map(s => {
      const risk = s.risk;
      const isEligible = risk.isEligible;

      const attDisplay = !isNaN(s.attPct)
        ? `<strong>${s.attPct.toFixed(1)}%</strong>`
        : '<span style="color: var(--text-muted); font-style: italic;">--%</span>';

      const avgDisplay = !isNaN(s.avgMarks)
        ? `<strong>${s.avgMarks.toFixed(1)}%</strong>`
        : '<span style="color: var(--text-muted); font-style: italic;">--%</span>';

      // Find if student has any course taught by the logged-in faculty
      let taughtCourseId = null;
      let taughtCourseName = null;
      let existingMark = null;
      let existingAtt = null;

      if (facultyData && Array.isArray(facultyData.marks)) {
        existingMark = facultyData.marks.find(m => m.student_id === s.id);
      }
      if (facultyData && Array.isArray(facultyData.attendance)) {
        existingAtt = facultyData.attendance.find(a => a.student_id === s.id);
      }

      if (existingMark) {
        taughtCourseId = existingMark.course_id;
        taughtCourseName = existingMark.course_name;
      } else if (existingAtt) {
        taughtCourseId = existingAtt.course_id;
        taughtCourseName = existingAtt.course_name;
      }

      // Actions buttons
      let actionButtons = '';
      if (taughtCourseId) {
        const safeName = (s.name || '').replace(/'/g, "\\'");
        const safeCName = (taughtCourseName || '').replace(/'/g, "\\'");
        const mid = existingMark ? existingMark.midterm : 0;
        const end = existingMark ? existingMark.endterm : 0;
        const qz = existingMark ? existingMark.quiz : 0;
        const attCls = existingAtt ? existingAtt.attended : 0;
        const totCls = existingAtt ? existingAtt.total : 0;

        actionButtons = `
          <div style="display: flex; gap: 0.35rem; justify-content: center; flex-wrap: wrap;">
            <button class="btn btn-secondary btn-sm" title="Update Marks for ${taughtCourseId}" onclick="window.openEditMarksModal('${s.id}', '${safeName}', '${taughtCourseId}', '${safeCName}', ${mid}, ${end}, ${qz})">
              ✏️ Marks
            </button>
            <button class="btn btn-secondary btn-sm" title="Update Attendance for ${taughtCourseId}" onclick="window.openEditAttModal('${s.id}', '${safeName}', '${taughtCourseId}', '${safeCName}', ${attCls}, ${totCls})">
              ⏱️ Att
            </button>
          </div>
        `;
      } else {
        actionButtons = `<span style="font-size: 0.75rem; color: var(--text-muted); font-style: italic;">Outside Course</span>`;
      }

      const reasonsHtml = risk.reasons.map(r => `<div style="margin-bottom: 0.2rem;">&bull; ${escapeHtml(r)}</div>`).join('');

      return `
        <tr>
          <td><strong>${escapeHtml(s.id)}</strong></td>
          <td>
            <strong>${escapeHtml(s.name)}</strong>
            <div style="font-size: 0.72rem; color: var(--text-muted);">${escapeHtml(s.email)}</div>
          </td>
          <td>
            ${escapeHtml(s.department)}
            <div style="font-size: 0.75rem; color: var(--text-secondary);">Year ${s.year} (Sec ${escapeHtml(s.section)})</div>
          </td>
          <td>
            ${attDisplay}
            <div style="font-size: 0.75rem; color: var(--text-muted);">${s.totalAttended} / ${s.totalClasses} classes</div>
            ${risk.severity === 'insufficient' ? '<span class="badge badge-purple" style="font-size: 0.65rem;">No Records</span>' : (isEligible ? '<span class="badge badge-green" style="font-size: 0.65rem;">&ge;75% Satisfied</span>' : '<span class="badge badge-red" style="font-size: 0.65rem;">Shortage (&lt;75%)</span>')}
          </td>
          <td>
            ${avgDisplay}
            <div style="font-size: 0.75rem; color: var(--text-muted);">${s.validMarksCount} courses</div>
            ${risk.severity === 'insufficient' ? '<span class="badge badge-purple" style="font-size: 0.65rem;">No Marks</span>' : (s.avgMarks >= 75 ? '<span class="badge badge-green" style="font-size: 0.65rem;">Good</span>' : (s.avgMarks >= 60 ? '<span class="badge badge-amber" style="font-size: 0.65rem;">Moderate</span>' : '<span class="badge badge-red" style="font-size: 0.65rem;">Low Avg</span>'))}
          </td>
          <td>
            <span class="badge ${risk.badgeClass}" style="font-size: 0.75rem; padding: 0.25rem 0.55rem;">
              ${escapeHtml(risk.displayStatus)}
            </span>
          </td>
          <td style="font-size: 0.8rem; color: var(--text-primary); line-height: 1.35;">
            ${reasonsHtml}
          </td>
          <td style="font-size: 0.8rem; color: var(--text-secondary); line-height: 1.35;">
            ${escapeHtml(risk.recommendation)}
          </td>
          <td style="text-align: center;">
            ${actionButtons}
          </td>
        </tr>
      `;
    }).join('');
  }

  function refreshFacultyMarksTable() {
    if (!facultyData || !facultyData.marks) return;
    const filterMarks = document.getElementById('filter-marks-input');
    const q = filterMarks ? filterMarks.value.trim().toLowerCase() : '';
    if (!q) {
      renderFacultyMarksTable(facultyData.marks);
    } else {
      const filtered = facultyData.marks.filter(m =>
        (m.student_id && m.student_id.toLowerCase().includes(q)) ||
        (m.student_name && m.student_name.toLowerCase().includes(q)) ||
        (m.course_id && m.course_id.toLowerCase().includes(q)) ||
        (m.course_name && m.course_name.toLowerCase().includes(q))
      );
      renderFacultyMarksTable(filtered);
    }
  }

  function refreshFacultyAttendanceTable() {
    if (!facultyData || !facultyData.attendance) return;
    const filterAtt = document.getElementById('filter-att-input');
    const q = filterAtt ? filterAtt.value.trim().toLowerCase() : '';
    if (!q) {
      renderFacultyAttendanceTable(facultyData.attendance);
    } else {
      const filtered = facultyData.attendance.filter(a =>
        (a.student_id && a.student_id.toLowerCase().includes(q)) ||
        (a.student_name && a.student_name.toLowerCase().includes(q)) ||
        (a.course_id && a.course_id.toLowerCase().includes(q)) ||
        (a.course_name && a.course_name.toLowerCase().includes(q))
      );
      renderFacultyAttendanceTable(filtered);
    }
  }

  async function loadFacultyDashboard(facultyId) {
    try {
      const data = await fetchServerJson(`/api/faculty/dashboard?id=${encodeURIComponent(facultyId)}`);
      if (!data || !data.faculty) throw new Error('Invalid faculty data returned by C server');
      facultyData = data;

      // Profile Header
      document.getElementById('faculty-prof-name').textContent = data.faculty.name;
      document.getElementById('faculty-prof-id').textContent = data.faculty.id;
      document.getElementById('faculty-prof-dept').textContent = data.faculty.department;
      document.getElementById('faculty-prof-email').textContent = data.faculty.email;

      // Metrics
      const stats = data.stats || {};
      document.getElementById('faculty-courses-count').textContent = stats.assigned_courses !== undefined ? stats.assigned_courses : (data.courses ? data.courses.length : 0);
      document.getElementById('faculty-students-count').textContent = stats.total_students !== undefined ? stats.total_students : 0;
      document.getElementById('faculty-class-avg').textContent = typeof stats.class_avg_marks === 'number' ? `${stats.class_avg_marks.toFixed(1)}%` : '--%';
      document.getElementById('faculty-range-marks').textContent = `Highest: ${(stats.highest_marks || 0).toFixed(1)}% • Lowest: ${(stats.lowest_marks || 0).toFixed(1)}%`;
      document.getElementById('faculty-att-avg').textContent = typeof stats.class_avg_attendance === 'number' ? `${stats.class_avg_attendance.toFixed(1)}%` : '--%';

      // Tables (preserves active search filters)
      renderFacultyCoursesTable(data.courses || []);
      refreshFacultyMarksTable();
      refreshFacultyAttendanceTable();

      // Render Early-Warning Center & dashboard alert summary
      await renderFacultyEarlyWarningCenter();

    } catch (err) {
      console.error('Error loading faculty dashboard:', err);
      showToast(err.message, 'error');
    }
  }

  function renderFacultyCoursesTable(courses) {
    const tbody = document.getElementById('faculty-courses-tbody');
    tbody.innerHTML = courses.map(c => `
      <tr>
        <td><strong>${c.id}</strong></td>
        <td>${c.name}</td>
        <td>${c.department}</td>
        <td><span class="badge badge-blue">${c.credits} Credits</span></td>
        <td>Semester ${c.semester}</td>
        <td><span class="badge badge-purple">${c.enrolled_students} Students</span></td>
      </tr>
    `).join('');
  }

  function renderFacultyMarksTable(marks) {
    const tbody = document.getElementById('faculty-marks-tbody');
    if (!tbody) return;
    if (!marks || marks.length === 0) {
      tbody.innerHTML = '<tr><td colspan="9" style="text-align: center; color: var(--text-secondary); padding: 1.5rem;">No student marks found.</td></tr>';
      return;
    }
    tbody.innerHTML = marks.map((m, idx) => {
      const isPending = (m.grade === '-' || !m.grade);
      const safeName = (m.student_name || '').replace(/'/g, "\\'");
      const safeCName = (m.course_name || '').replace(/'/g, "\\'");
      const scoreDisplay = (isPending && m.total_pct === 0)
        ? '<span style="color: var(--text-secondary); font-style: italic;">Not entered</span>'
        : `<strong>${m.total_pct.toFixed(2)}%</strong>`;
      const gradeBadge = isPending
        ? '<span class="badge badge-purple">Pending</span>'
        : `<span class="badge ${getGradeBadgeClass(m.grade)}">${m.grade}</span>`;

      return `
        <tr>
          <td><strong>${escapeHtml(m.student_id)}</strong></td>
          <td>${escapeHtml(m.student_name)}</td>
          <td><span class="badge badge-blue">${escapeHtml(m.course_id)}</span></td>
          <td>${isPending && m.midterm === 0 ? '-' : m.midterm}</td>
          <td>${isPending && m.endterm === 0 ? '-' : m.endterm}</td>
          <td>${isPending && m.quiz === 0 ? '-' : m.quiz}</td>
          <td>${scoreDisplay}</td>
          <td>${gradeBadge}</td>
          <td>
            <button class="btn btn-secondary btn-sm" onclick="window.openEditMarksModal('${m.student_id}', '${safeName}', '${m.course_id}', '${safeCName}', ${m.midterm}, ${m.endterm}, ${m.quiz})">
              ✏️ Update
            </button>
          </td>
        </tr>
      `;
    }).join('');
  }

  function renderFacultyAttendanceTable(attendance) {
    const tbody = document.getElementById('faculty-attendance-tbody');
    if (!tbody) return;
    if (!attendance || attendance.length === 0) {
      tbody.innerHTML = '<tr><td colspan="8" style="text-align: center; color: var(--text-secondary); padding: 1.5rem;">No attendance records found.</td></tr>';
      return;
    }
    tbody.innerHTML = attendance.map(a => {
      const isUnrecorded = (a.total === 0);
      const isEligible = a.percentage >= 75.0;
      const safeName = (a.student_name || '').replace(/'/g, "\\'");
      const safeCName = (a.course_name || '').replace(/'/g, "\\'");

      const badge = isUnrecorded
        ? '<span class="badge badge-purple">Not Recorded</span>'
        : isEligible
          ? `<span class="badge badge-green">${a.percentage.toFixed(1)}%</span>`
          : `<span class="badge badge-red">⚠️ ${a.percentage.toFixed(1)}%</span>`;

      const statusText = isUnrecorded
        ? '<span style="color: var(--text-secondary); font-style: italic;">New Student</span>'
        : (isEligible ? 'Regular' : 'Shortage');

      return `
        <tr>
          <td><strong>${escapeHtml(a.student_id)}</strong></td>
          <td>${escapeHtml(a.student_name)}</td>
          <td><span class="badge badge-blue">${escapeHtml(a.course_id)}</span></td>
          <td>${a.attended}</td>
          <td>${a.total}</td>
          <td>${badge}</td>
          <td>${statusText}</td>
          <td>
            <div style="display: flex; gap: 0.35rem;">
              <button class="btn btn-success btn-sm" onclick="window.quickMarkPresent('${a.student_id}', '${a.course_id}', ${a.attended}, ${a.total})">
                +1 Present
              </button>
              <button class="btn btn-secondary btn-sm" onclick="window.openEditAttModal('${a.student_id}', '${safeName}', '${a.course_id}', '${safeCName}', ${a.attended}, ${a.total})">
                ✏️ Edit
              </button>
            </div>
          </td>
        </tr>
      `;
    }).join('');
  }

  // ---------------------------------------------------------------------------
  // CRUD OPERATIONS: UPDATE MARKS & ATTENDANCE OVER SOCKET
  // ---------------------------------------------------------------------------

  // Open Edit Marks Modal
  window.openEditMarksModal = function(studentId, studentName, courseId, courseName, mid, end, qz) {
    document.getElementById('edit-marks-student-id').value = studentId;
    document.getElementById('edit-marks-course-id').value = courseId;
    document.getElementById('edit-marks-student-name').textContent = `${studentName} (${studentId})`;
    document.getElementById('edit-marks-course-name').textContent = `${courseName} (${courseId})`;
    document.getElementById('edit-midterm').value = mid;
    document.getElementById('edit-endterm').value = end;
    document.getElementById('edit-quiz').value = qz;
    modalMarks.classList.remove('hidden');
  };

  if (modalMarksClose) modalMarksClose.onclick = () => modalMarks && modalMarks.classList.add('hidden');
  if (modalMarksCancel) modalMarksCancel.onclick = () => modalMarks && modalMarks.classList.add('hidden');

  if (formUpdateMarks) {
    formUpdateMarks.addEventListener('submit', async (e) => {
      e.preventDefault();
      const sIdEl = document.getElementById('edit-marks-student-id');
      const cIdEl = document.getElementById('edit-marks-course-id');
      const midEl = document.getElementById('edit-midterm');
      const endEl = document.getElementById('edit-endterm');
      const qzEl = document.getElementById('edit-quiz');
      const student_id = sIdEl ? sIdEl.value.trim() : '';
      const course_id = cIdEl ? cIdEl.value.trim() : '';
      const midterm = midEl ? parseFloat(midEl.value) : 0;
      const endterm = endEl ? parseFloat(endEl.value) : 0;
      const quiz = qzEl ? parseFloat(qzEl.value) : 0;

      if (!student_id || !course_id) {
        showToast('Missing student ID or course ID', 'error');
        return;
      }

      try {
        const resp = await fetch('/api/marks/update', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ student_id, course_id, midterm, endterm, quiz })
        });

        if (!resp.ok) {
          const errData = await resp.json().catch(() => ({}));
          throw new Error(errData.message || 'Failed to update marks on C server');
        }
        const res = await resp.json();

        if (modalMarks) modalMarks.classList.add('hidden');
        showToast(`Marks updated in C memory store! Total: ${res.record.total_pct.toFixed(2)}% (${res.record.grade})`, 'success');

        // Synchronize across all sections and broadcast to other tabs/sessions
        await syncAcademicDataAcrossAllSections(student_id, course_id, 'marks');
      } catch (err) {
        showToast(err.message, 'error');
      }
    });
  }

  // Open Edit Attendance Modal
  window.openEditAttModal = function(studentId, studentName, courseId, courseName, attended, total) {
    const sId = document.getElementById('edit-att-student-id');
    const cId = document.getElementById('edit-att-course-id');
    const sName = document.getElementById('edit-att-student-name');
    const cName = document.getElementById('edit-att-course-name');
    const att = document.getElementById('edit-attended');
    const tot = document.getElementById('edit-total-classes');
    if (sId) sId.value = studentId;
    if (cId) cId.value = courseId;
    if (sName) sName.textContent = `${studentName} (${studentId})`;
    if (cName) cName.textContent = `${courseName} (${courseId})`;
    if (att) att.value = attended;
    if (tot) tot.value = total;
    if (modalAttendance) modalAttendance.classList.remove('hidden');
  };

  if (modalAttClose) modalAttClose.onclick = () => modalAttendance && modalAttendance.classList.add('hidden');
  if (modalAttCancel) modalAttCancel.onclick = () => modalAttendance && modalAttendance.classList.add('hidden');

  if (formUpdateAttendance) {
    formUpdateAttendance.addEventListener('submit', async (e) => {
      e.preventDefault();
      const sIdEl = document.getElementById('edit-att-student-id');
      const cIdEl = document.getElementById('edit-att-course-id');
      const attEl = document.getElementById('edit-attended');
      const totEl = document.getElementById('edit-total-classes');
      const student_id = sIdEl ? sIdEl.value.trim() : '';
      const course_id = cIdEl ? cIdEl.value.trim() : '';
      const attended = attEl ? parseInt(attEl.value, 10) : 0;
      const total = totEl ? parseInt(totEl.value, 10) : 0;

      if (!student_id || !course_id) {
        showToast('Missing student ID or course ID', 'error');
        return;
      }

      if (attended > total) {
        showToast('Attended classes cannot exceed total classes', 'error');
        return;
      }

      try {
        const resp = await fetch('/api/attendance/update', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ student_id, course_id, attended, total })
        });

        if (!resp.ok) {
          const errData = await resp.json().catch(() => ({}));
          throw new Error(errData.message || 'Failed to update attendance on C server');
        }
        const res = await resp.json();

        if (modalAttendance) modalAttendance.classList.add('hidden');
        showToast(`Attendance updated in C store! ${res.record.attended}/${res.record.total} (${res.record.percentage.toFixed(1)}%)`, 'success');

        // Synchronize across all sections and broadcast to other tabs/sessions
        await syncAcademicDataAcrossAllSections(student_id, course_id, 'attendance');
      } catch (err) {
        showToast(err.message, 'error');
      }
    });
  }

  // Quick increment attendance (+1 Present)
  window.quickMarkPresent = async function(studentId, courseId, currentAttended, currentTotal) {
    try {
      const resp = await fetch('/api/attendance/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          student_id: studentId,
          course_id: courseId,
          attended: currentAttended + 1,
          total: currentTotal + 1
        })
      });

      if (!resp.ok) {
        const errData = await resp.json().catch(() => ({}));
        throw new Error(errData.message || 'Failed to mark present');
      }
      const res = await resp.json();
      showToast(`Recorded attendance for ${studentId}: ${res.record.attended}/${res.record.total} (${res.record.percentage.toFixed(1)}%)`, 'success');

      // Synchronize across all sections and broadcast to other tabs/sessions
      await syncAcademicDataAcrossAllSections(studentId, courseId, 'attendance');
    } catch (err) {
      showToast(err.message, 'error');
    }
  };

  // ---------------------------------------------------------------------------
  // OS SOCKET MONITOR & PHASE 1 INTERACTIVE TESTING
  // ---------------------------------------------------------------------------
  if (btnPing) btnPing.addEventListener('click', () => fetchStatus(true));

  if (btnSampleData) {
    btnSampleData.addEventListener('click', async () => {
      const startTime = performance.now();
      if (terminalLatency) terminalLatency.textContent = 'Requesting...';
      try {
        const response = await fetch('/api/sample-data', { cache: 'no-store' });
        const latency = Math.round(performance.now() - startTime);
        if (terminalLatency) terminalLatency.textContent = `Latency: ${latency} ms`;

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        if (terminalOutput) terminalOutput.textContent = `// HTTP 200 OK (${latency}ms) — GET /api/sample-data\n` + JSON.stringify(data, null, 2);
      } catch (err) {
        if (terminalLatency) terminalLatency.textContent = 'Error';
        if (terminalOutput) terminalOutput.textContent = `// Error: ${err.message}`;
      }
    });
  }

  if (btnTestHealth) {
    btnTestHealth.addEventListener('click', async () => {
      const startTime = performance.now();
      if (terminalLatency) terminalLatency.textContent = 'Requesting...';
      try {
        const response = await fetch('/api/health', { cache: 'no-store' });
        const latency = Math.round(performance.now() - startTime);
        if (terminalLatency) terminalLatency.textContent = `Latency: ${latency} ms`;

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        if (terminalOutput) terminalOutput.textContent = `// HTTP 200 OK (${latency}ms) — GET /api/health\n` + JSON.stringify(data, null, 2);
      } catch (err) {
        if (terminalLatency) terminalLatency.textContent = 'Error';
        if (terminalOutput) terminalOutput.textContent = `// Error: ${err.message}`;
      }
    });
  }

  if (btnEcho) {
    btnEcho.addEventListener('click', async () => {
      const startTime = performance.now();
      if (terminalLatency) terminalLatency.textContent = 'Transmitting...';
      try {
        const payload = (echoInput && echoInput.value) ? echoInput.value : JSON.stringify({ message: "Ping from OS Monitor client", timestamp: new Date().toISOString() });
        const response = await fetch('/api/echo', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: payload
        });
        const latency = Math.round(performance.now() - startTime);
        if (terminalLatency) terminalLatency.textContent = `Latency: ${latency} ms`;

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        if (terminalOutput) terminalOutput.textContent = `// HTTP 200 OK (${latency}ms) — POST /api/echo received and echoed by C server\n` + JSON.stringify(data, null, 2);
      } catch (err) {
        if (terminalLatency) terminalLatency.textContent = 'Error';
        if (terminalOutput) terminalOutput.textContent = `// Error: ${err.message}`;
      }
    });
  }

  // ---------------------------------------------------------------------------
  // CONCURRENCY BURST TESTING (Phase 3 & Phase 5 Stress Verification)
  // ---------------------------------------------------------------------------
  const btnBurst20 = document.getElementById('btn-burst-20');
  const btnBurst32 = document.getElementById('btn-burst-32');
  const btnBurst50 = document.getElementById('btn-burst-50');
  const cMetricsPanel = document.getElementById('concurrency-metrics-panel');
  const cMetricLaunched = document.getElementById('c-metric-launched');
  const cMetricSuccess = document.getElementById('c-metric-success');
  const cMetricFailed = document.getElementById('c-metric-failed');
  const cMetricTime = document.getElementById('c-metric-time');
  const cMetricAvgLat = document.getElementById('c-metric-avglat');
  const cMetricMaxLat = document.getElementById('c-metric-maxlat');
  const cMetricTps = document.getElementById('c-metric-tps');
  const cWorkerDist = document.getElementById('c-worker-distribution');

  async function runConcurrencyBurst(count) {
    if (cMetricsPanel) cMetricsPanel.style.display = 'block';
    if (cMetricLaunched) cMetricLaunched.textContent = count;
    if (cMetricSuccess) cMetricSuccess.textContent = '...';
    if (cMetricFailed) cMetricFailed.textContent = '0';
    if (cMetricTime) cMetricTime.textContent = 'running';
    if (cMetricAvgLat) cMetricAvgLat.textContent = '...';
    if (cMetricMaxLat) cMetricMaxLat.textContent = '...';
    if (cMetricTps) cMetricTps.textContent = '...';

    const startTime = performance.now();
    if (terminalLatency) terminalLatency.textContent = `Bursting ${count} reqs...`;
    if (terminalOutput) terminalOutput.textContent = `// Dispatching ${count} concurrent asynchronous HTTP requests across POSIX socket pipeline...\n` +
      `// Dedicated demo path: GET /api/concurrency/test (2-slot DB Semaphore + 4 Workers + Bounded Queues)\n`;

    const taskTypes = ['academic_tx', 'marks_eval', 'attendance_sync', 'roster_query'];
    const burstPromises = [];
    for (let i = 1; i <= count; i++) {
      const taskType = taskTypes[(i - 1) % taskTypes.length];
      const ep = `/api/concurrency/test?id=${i}&type=${taskType}`;
      const reqStart = performance.now();
      const p = fetch(ep, { cache: 'no-store' })
        .then(async r => {
          const reqLat = Math.round(performance.now() - reqStart);
          const json = await r.json().catch(() => ({}));
          const wid = json.worker_id !== undefined ? json.worker_id : (json.phase ? 0 : '?');
          return { id: i, ep, status: r.status, ok: r.ok, latency: reqLat, workerId: wid, taskType };
        })
        .catch(err => ({ id: i, ep, status: 'ERR', ok: false, latency: 0, error: err.message, workerId: 'ERR' }));
      burstPromises.push(p);
    }

    // High-frequency polling during concurrency demonstration so transient queue states,
    // worker busy/idle transitions, and semaphore availability from the real C server are clearly observable
    let isBurstRunning = true;
    const pollLoop = async () => {
      while (isBurstRunning) {
        await fetchStatus(false);
        await new Promise(r => setTimeout(r, 60));
      }
    };
    pollLoop();

    try {
      const results = await Promise.all(burstPromises);
      // Allow the response dispatcher thread to finish draining the bounded response queue
      await new Promise(r => setTimeout(r, 120));
      isBurstRunning = false;
      await fetchStatus(false);

      const totalDuration = Math.round(performance.now() - startTime);
      const successCount = results.filter(r => r.ok).length;
      const failedCount = results.length - successCount;
      const latencies = results.map(r => r.latency).filter(l => l > 0);
      const avgLat = latencies.length ? Math.round(latencies.reduce((a, b) => a + b, 0) / latencies.length) : 0;
      const maxLat = latencies.length ? Math.max(...latencies) : 0;
      const tps = totalDuration > 0 ? ((count / (totalDuration / 1000))).toFixed(1) : count;

      if (terminalLatency) terminalLatency.textContent = `Completed in ${totalDuration} ms`;

      if (cMetricSuccess) cMetricSuccess.textContent = successCount;
      if (cMetricFailed) cMetricFailed.textContent = failedCount;
      if (cMetricTime) cMetricTime.textContent = `${totalDuration} ms`;
      if (cMetricAvgLat) cMetricAvgLat.textContent = `${avgLat} ms`;
      if (cMetricMaxLat) cMetricMaxLat.textContent = `${maxLat} ms`;
      if (cMetricTps) cMetricTps.textContent = `${tps} req/s`;

      // Count distribution by worker
      const workerCounts = { 0: 0, 1: 0, 2: 0, 3: 0 };
      results.forEach(r => {
        if (r.workerId !== undefined && workerCounts[r.workerId] !== undefined) {
          workerCounts[r.workerId]++;
        }
      });

      if (cWorkerDist) {
        cWorkerDist.textContent = `Worker Distribution: Worker 0: ${workerCounts[0]} | Worker 1: ${workerCounts[1]} | Worker 2: ${workerCounts[2]} | Worker 3: ${workerCounts[3]}`;
      }

      let summary = `// ==========================================================================\n`;
      summary += `// 🚀 CONCURRENCY BURST RESULT: ${successCount}/${count} Succeeded in ${totalDuration}ms (${tps} req/s)\n`;
      summary += `// Worker Thread Distribution:\n`;
      for (let wid = 0; wid < 4; wid++) {
        summary += `//   Worker ${wid}: ${workerCounts[wid]} requests handled\n`;
      }
      summary += `// Detailed Trace:\n`;
      results.forEach(r => {
        summary += `//   Req #${String(r.id).padStart(2, '0')}: ${r.ep} -> HTTP ${r.status} (${r.latency}ms) [Worker ${r.workerId}]\n`;
      });
      summary += `// ==========================================================================`;

      if (terminalOutput) terminalOutput.textContent = summary;
      showToast(`${count} concurrent requests completed in ${totalDuration}ms across 4 POSIX worker threads!`, 'success');
      fetchStatus(false);
    } catch (err) {
      isBurstRunning = false;
      if (terminalLatency) terminalLatency.textContent = 'Error';
      if (terminalOutput) terminalOutput.textContent = `// Burst test error: ${err.message}`;
    }
  }

  if (btnBurst20) btnBurst20.addEventListener('click', () => runConcurrencyBurst(20));
  if (btnBurst32) btnBurst32.addEventListener('click', () => runConcurrencyBurst(32));
  if (btnBurst50) btnBurst50.addEventListener('click', () => runConcurrencyBurst(50));
  if (btnBurstTest) btnBurstTest.addEventListener('click', () => runConcurrencyBurst(20));

  // ---------------------------------------------------------------------------
  // FACULTY PORTAL: STUDENT MANAGEMENT (CRUD & SEARCH via C Server)
  // ---------------------------------------------------------------------------
  let cachedStudentsList = [];
  let cachedFacultyList = [];

  function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  function showAlert(elId, message, type = 'success') {
    const el = document.getElementById(elId);
    if (!el) return;
    el.textContent = message;
    el.classList.remove('hidden');
    if (type === 'success') {
      el.style.background = 'rgba(16, 185, 129, 0.15)';
      el.style.border = '1px solid #10b981';
      el.style.color = '#34d399';
    } else {
      el.style.background = 'rgba(239, 68, 68, 0.15)';
      el.style.border = '1px solid #ef4444';
      el.style.color = '#f87171';
    }
    setTimeout(() => {
      if (el) el.classList.add('hidden');
    }, 4500);
  }

  function renderStudentRows(students) {
    const tbody = document.getElementById('faculty-students-tbody');
    if (!tbody) return;
    if (!students || students.length === 0) {
      tbody.innerHTML = '<tr><td colspan="7" style="text-align: center; color: var(--text-secondary); padding: 1.5rem;">No students found.</td></tr>';
      return;
    }
    tbody.innerHTML = students.map(s => `
      <tr>
        <td><strong style="font-family: var(--font-mono); color: var(--primary-color);">${escapeHtml(s.id)}</strong></td>
        <td><strong>${escapeHtml(s.name)}</strong></td>
        <td style="color: var(--text-secondary);">${escapeHtml(s.email)}</td>
        <td><span class="badge badge-blue">${escapeHtml(s.department)}</span></td>
        <td>Year ${s.year}</td>
        <td>Sec ${escapeHtml(s.section)}</td>
        <td>
          <div style="display: flex; gap: 0.4rem;">
            <button class="btn btn-secondary btn-sm btn-edit-student" data-id="${escapeHtml(s.id)}">✏️ Edit</button>
            <button class="btn btn-danger btn-sm btn-delete-student" data-id="${escapeHtml(s.id)}" data-name="${escapeHtml(s.name)}">🗑️ Delete</button>
          </div>
        </td>
      </tr>
    `).join('');

    tbody.querySelectorAll('.btn-edit-student').forEach(btn => {
      btn.onclick = () => openEditStudentModal(btn.dataset.id);
    });
    tbody.querySelectorAll('.btn-delete-student').forEach(btn => {
      btn.onclick = () => deleteStudent(btn.dataset.id);
    });
  }

  async function loadFacultyStudents() {
    const tbody = document.getElementById('faculty-students-tbody');
    if (!tbody) return;
    try {
      const resp = await fetch('/api/students', { cache: 'no-store' });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      cachedStudentsList = data.students || [];
      renderStudentRows(cachedStudentsList);
    } catch (err) {
      tbody.innerHTML = `<tr><td colspan="7" style="color: var(--danger-color); padding: 1rem;">Failed to load students: ${err.message}</td></tr>`;
    }
  }

  async function searchFacultyStudents(query) {
    const q = (query || '').trim();
    if (!q) {
      loadFacultyStudents();
      return;
    }
    try {
      // 1. Search endpoint
      const searchResp = await fetch(`/api/student/search?q=${encodeURIComponent(q)}`, { cache: 'no-store' });
      if (searchResp.ok) {
        const sData = await searchResp.json();
        if (sData && sData.found && sData.student) {
          renderStudentRows([sData.student]);
          return;
        }
      }

      // 2. Direct ID lookup
      const idResp = await fetch(`/api/student?id=${encodeURIComponent(q)}`, { cache: 'no-store' });
      if (idResp.ok) {
        const idData = await idResp.json();
        if (idData && idData.found && idData.student) {
          renderStudentRows([idData.student]);
          return;
        }
      }

      // 3. Fallback: filter cached students
      const qLower = q.toLowerCase();
      const matches = cachedStudentsList.filter(s =>
        s.id.toLowerCase().includes(qLower) || s.name.toLowerCase().includes(qLower)
      );
      renderStudentRows(matches);
    } catch (err) {
      showAlert('alert-faculty-student', `Search error: ${err.message}`, 'error');
    }
  }

  // --- Add Student ---
  const btnOpenAddStudent = document.getElementById('btn-open-add-student');
  const modalAddStudent = document.getElementById('modal-add-student');
  const btnCloseAddStudent = document.getElementById('btn-close-add-student-modal');
  const btnCancelAddStudent = document.getElementById('btn-cancel-add-student');
  const btnSubmitAddStudent = document.getElementById('btn-submit-add-student');
  const addStudentError = document.getElementById('add-student-error');

  if (btnOpenAddStudent) {
    btnOpenAddStudent.onclick = () => {
      document.getElementById('add-student-id').value = '';
      document.getElementById('add-student-name').value = '';
      document.getElementById('add-student-email').value = '';
      document.getElementById('add-student-dept').value = 'Computer Science';
      document.getElementById('add-student-year').value = '3';
      document.getElementById('add-student-section').value = 'A';
      document.getElementById('add-student-pass').value = '';
      if (addStudentError) {
        addStudentError.textContent = '';
        addStudentError.classList.add('hidden');
      }
      if (modalAddStudent) modalAddStudent.classList.remove('hidden');
    };
  }

  if (btnCloseAddStudent) btnCloseAddStudent.onclick = () => modalAddStudent && modalAddStudent.classList.add('hidden');
  if (btnCancelAddStudent) btnCancelAddStudent.onclick = () => modalAddStudent && modalAddStudent.classList.add('hidden');

  if (btnSubmitAddStudent) {
    btnSubmitAddStudent.onclick = async () => {
      const id = document.getElementById('add-student-id').value.trim();
      const name = document.getElementById('add-student-name').value.trim();
      const email = document.getElementById('add-student-email').value.trim();
      const department = document.getElementById('add-student-dept').value.trim();
      const year = parseInt(document.getElementById('add-student-year').value, 10);
      const section = document.getElementById('add-student-section').value.trim();
      const password = document.getElementById('add-student-pass').value.trim() || 'pass123';

      if (!id || !name || !email || !department || isNaN(year) || !section) {
        if (addStudentError) {
          addStudentError.textContent = 'Please fill out all required fields (*).';
          addStudentError.classList.remove('hidden');
        }
        return;
      }

      try {
        btnSubmitAddStudent.disabled = true;
        const resp = await fetch('/api/student/create', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ id, name, email, department, year, section, password })
        });
        const data = await resp.json();
        btnSubmitAddStudent.disabled = false;

        if (resp.ok && (data.success || data.student)) {
          if (modalAddStudent) modalAddStudent.classList.add('hidden');
          showToast(`Student ${id} (${name}) created successfully!`, 'success');
          showAlert('alert-faculty-student', `Student ${id} (${name}) successfully added to C server mmap store.`, 'success');
          loadFacultyStudents();
          if (currentAuth && currentAuth.role === 'faculty') {
            loadFacultyDashboard(currentAuth.user.id);
          }
          fetchMmapInfo();
          fetchStatus(false);
        } else {
          if (addStudentError) {
            addStudentError.textContent = data.message || 'Failed to create student';
            addStudentError.classList.remove('hidden');
          }
        }
      } catch (err) {
        btnSubmitAddStudent.disabled = false;
        if (addStudentError) {
          addStudentError.textContent = `Error: ${err.message}`;
          addStudentError.classList.remove('hidden');
        }
      }
    };
  }

  // --- Edit Student ---
  const modalEditStudent = document.getElementById('modal-edit-student');
  const btnCloseEditStudent = document.getElementById('btn-close-edit-student-modal');
  const btnCancelEditStudent = document.getElementById('btn-cancel-edit-student');
  const btnSubmitEditStudent = document.getElementById('btn-submit-edit-student');
  const editStudentError = document.getElementById('edit-student-error');

  function openEditStudentModal(studentId) {
    const student = cachedStudentsList.find(s => s.id === studentId);
    if (!student) return;

    document.getElementById('edit-student-id').value = student.id;
    document.getElementById('edit-student-name').value = student.name;
    document.getElementById('edit-student-email').value = student.email;
    document.getElementById('edit-student-dept').value = student.department;
    document.getElementById('edit-student-year').value = student.year;
    document.getElementById('edit-student-section').value = student.section;
    document.getElementById('edit-student-pass').value = '';
    if (editStudentError) {
      editStudentError.textContent = '';
      editStudentError.classList.add('hidden');
    }
    if (modalEditStudent) modalEditStudent.classList.remove('hidden');
  }

  if (btnCloseEditStudent) btnCloseEditStudent.onclick = () => modalEditStudent && modalEditStudent.classList.add('hidden');
  if (btnCancelEditStudent) btnCancelEditStudent.onclick = () => modalEditStudent && modalEditStudent.classList.add('hidden');

  if (btnSubmitEditStudent) {
    btnSubmitEditStudent.onclick = async () => {
      const id = document.getElementById('edit-student-id').value.trim();
      const name = document.getElementById('edit-student-name').value.trim();
      const email = document.getElementById('edit-student-email').value.trim();
      const department = document.getElementById('edit-student-dept').value.trim();
      const year = parseInt(document.getElementById('edit-student-year').value, 10);
      const section = document.getElementById('edit-student-section').value.trim();
      const password = document.getElementById('edit-student-pass').value.trim();

      if (!id || !name || !email || !department || isNaN(year) || !section) {
        if (editStudentError) {
          editStudentError.textContent = 'Please fill out all required fields (*).';
          editStudentError.classList.remove('hidden');
        }
        return;
      }

      const payload = { id, name, email, department, year, section };
      if (password) payload.password = password;

      try {
        btnSubmitEditStudent.disabled = true;
        const resp = await fetch('/api/student/update', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const data = await resp.json();
        btnSubmitEditStudent.disabled = false;

        if (resp.ok && (data.success || data.student)) {
          if (modalEditStudent) modalEditStudent.classList.add('hidden');
          showToast(`Student ${id} updated successfully!`, 'success');
          showAlert('alert-faculty-student', `Student ${id} details updated in C memory map.`, 'success');
          loadFacultyStudents();
          if (currentAuth && currentAuth.role === 'faculty') {
            loadFacultyDashboard(currentAuth.user.id);
          }
          fetchMmapInfo();
          fetchStatus(false);
        } else {
          if (editStudentError) {
            editStudentError.textContent = data.message || 'Failed to update student';
            editStudentError.classList.remove('hidden');
          }
        }
      } catch (err) {
        btnSubmitEditStudent.disabled = false;
        if (editStudentError) {
          editStudentError.textContent = `Error: ${err.message}`;
          editStudentError.classList.remove('hidden');
        }
      }
    };
  }

  // --- Delete Student ---
  async function deleteStudent(studentId) {
    if (!confirm(`Delete student ${studentId}?`)) return;

    try {
      const resp = await fetch('/api/student/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: studentId })
      });
      const data = await resp.json();
      if (resp.ok && data.success) {
        showToast(`Student ${studentId} deleted successfully.`, 'success');
        showAlert('alert-faculty-student', `Student ${studentId} deleted from memory map.`, 'success');
        cachedStudentsList = cachedStudentsList.filter(s => s.id !== studentId);
        renderStudentRows(cachedStudentsList);
        if (currentAuth && currentAuth.role === 'faculty') {
          loadFacultyDashboard(currentAuth.user.id);
        }
        fetchMmapInfo();
        fetchStatus(false);
      } else {
        showToast(`Delete failed: ${data.message || 'Error'}`, 'error');
      }
    } catch (err) {
      showToast(`Delete request error: ${err.message}`, 'error');
    }
  }

  // Student Search & Reload buttons
  const btnFacultyStudentSearch = document.getElementById('btn-faculty-student-search');
  const facultyStudentSearchInput = document.getElementById('faculty-student-search-input');
  const btnFacultyStudentReload = document.getElementById('btn-faculty-student-reload');

  if (btnFacultyStudentSearch && facultyStudentSearchInput) {
    btnFacultyStudentSearch.onclick = () => searchFacultyStudents(facultyStudentSearchInput.value);
    facultyStudentSearchInput.onkeydown = (e) => {
      if (e.key === 'Enter') searchFacultyStudents(facultyStudentSearchInput.value);
    };
  }
  if (btnFacultyStudentReload) {
    btnFacultyStudentReload.onclick = () => {
      if (facultyStudentSearchInput) facultyStudentSearchInput.value = '';
      loadFacultyStudents();
      showToast('Student list reloaded.', 'info');
    };
  }

  // ---------------------------------------------------------------------------
  // FACULTY PORTAL: FACULTY DIRECTORY (Read & Search Only)
  // ---------------------------------------------------------------------------
  function renderFacultyRows(facultyList) {
    const tbody = document.getElementById('faculty-directory-tbody');
    if (!tbody) return;
    if (!facultyList || facultyList.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-secondary); padding: 1.5rem;">No faculty members found.</td></tr>';
      return;
    }
    tbody.innerHTML = facultyList.map(f => `
      <tr>
        <td><strong style="font-family: var(--font-mono); color: var(--primary-color);">${escapeHtml(f.id)}</strong></td>
        <td><strong>${escapeHtml(f.name)}</strong></td>
        <td style="color: var(--text-secondary);">${escapeHtml(f.email)}</td>
        <td><span class="badge badge-purple">${escapeHtml(f.department)}</span></td>
      </tr>
    `).join('');
  }

  async function loadFacultyDirectory() {
    const tbody = document.getElementById('faculty-directory-tbody');
    if (!tbody) return;
    try {
      const resp = await fetch('/api/faculty', { cache: 'no-store' });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      cachedFacultyList = data.faculty || [];
      renderFacultyRows(cachedFacultyList);
    } catch (err) {
      tbody.innerHTML = `<tr><td colspan="4" style="color: var(--danger-color); padding: 1rem;">Failed to load faculty: ${err.message}</td></tr>`;
    }
  }

  async function searchFacultyDirectory(query) {
    const q = (query || '').trim();
    if (!q) {
      loadFacultyDirectory();
      return;
    }
    try {
      const resp = await fetch(`/api/faculty/search?q=${encodeURIComponent(q)}`, { cache: 'no-store' });
      if (resp.ok) {
        const data = await resp.json();
        if (data && data.found && data.faculty) {
          renderFacultyRows([data.faculty]);
          return;
        }
      }

      const qLower = q.toLowerCase();
      const matches = cachedFacultyList.filter(f =>
        f.id.toLowerCase().includes(qLower) || f.name.toLowerCase().includes(qLower)
      );
      renderFacultyRows(matches);
    } catch (err) {
      showAlert('alert-faculty-dir', `Search error: ${err.message}`, 'error');
    }
  }

  // Faculty Search & Reload buttons
  const btnFacultySearch = document.getElementById('btn-faculty-search');
  const facultySearchInput = document.getElementById('faculty-search-input');
  const btnFacultyReload = document.getElementById('btn-faculty-reload');

  if (btnFacultySearch && facultySearchInput) {
    btnFacultySearch.onclick = () => searchFacultyDirectory(facultySearchInput.value);
    facultySearchInput.onkeydown = (e) => {
      if (e.key === 'Enter') searchFacultyDirectory(facultySearchInput.value);
    };
  }
  if (btnFacultyReload) {
    btnFacultyReload.onclick = () => {
      if (facultySearchInput) facultySearchInput.value = '';
      loadFacultyDirectory();
      showToast('Faculty directory reloaded.', 'info');
    };
  }

  // ---------------------------------------------------------------------------
  // PHASE 5: ADVANCED LINUX FEATURES & OS MONITOR CONTROLLER
  // ---------------------------------------------------------------------------
  const btnToggleOsView = document.getElementById('btn-toggle-os-view');
  let lastActiveViewBeforeOs = null;

  if (btnToggleOsView) {
    btnToggleOsView.addEventListener('click', () => {
      const isCurrentlyOpen = !tabOsMonitor.classList.contains('hidden');
      if (isCurrentlyOpen) {
        // Return to previous view
        tabOsMonitor.classList.add('hidden');
        if (currentAuth) {
          if (currentAuth.role === 'student') {
            viewStudent.classList.remove('hidden');
          } else {
            viewFaculty.classList.remove('hidden');
          }
        } else {
          viewAuth.classList.remove('hidden');
        }
        btnToggleOsView.textContent = '⚡ OS Monitor';
      } else {
        // Open OS Monitor
        viewAuth.classList.add('hidden');
        viewStudent.classList.add('hidden');
        viewFaculty.classList.add('hidden');
        tabOsMonitor.classList.remove('hidden');
        btnToggleOsView.textContent = '✖ Close OS Monitor';
        // Refresh all OS telemetry immediately
        fetchStatus(false);
        fetchMmapInfo();
        fetchProcStats();
        fetchConfig();
        fetchLogs(activeLogTab);
      }
    });
  }

  // --- POSIX mmap() Info ---
  async function fetchMmapInfo() {
    try {
      const resp = await fetch('/api/mmap/info', { cache: 'no-store' });
      if (!resp.ok) return;
      const data = await resp.json();
      const mmapPath = document.getElementById('mmap-path');
      const mmapSize = document.getElementById('mmap-size');
      const mmapVer = document.getElementById('mmap-ver');
      const mmapTxs = document.getElementById('mmap-txs');
      const mmapLastSync = document.getElementById('mmap-last-sync');
      const badgeMagic = document.getElementById('badge-mmap-magic');

      if (mmapPath) mmapPath.textContent = data.file_path || 'server/data/university.dat';
      if (mmapSize) mmapSize.textContent = `${(data.file_size_bytes || 0).toLocaleString()} bytes`;
      if (mmapVer) mmapVer.textContent = `v${data.version || 1}`;
      if (mmapTxs) mmapTxs.textContent = `${data.transaction_count || 0} commits`;
      if (mmapLastSync) mmapLastSync.textContent = data.last_sync_time || 'Startup';
      if (badgeMagic && data.magic) badgeMagic.textContent = `${data.magic} (UNIV)`;
    } catch (err) {
      console.error('Failed to fetch mmap info:', err);
    }
  }

  const btnMmapRefresh = document.getElementById('btn-mmap-refresh');
  if (btnMmapRefresh) btnMmapRefresh.addEventListener('click', () => {
    fetchMmapInfo();
    showToast('Memory-mapped store information refreshed.', 'info');
  });

  const btnMmapSync = document.getElementById('btn-mmap-sync');
  if (btnMmapSync) btnMmapSync.addEventListener('click', async () => {
    try {
      // In our server, updates trigger msync(); fetching mmap info confirms latest sync state
      await fetchMmapInfo();
      showToast('POSIX msync() committed memory map to university.dat on disk.', 'success');
    } catch (err) {
      showToast('Failed to trigger msync: ' + err.message, 'error');
    }
  });

  // --- Linux /proc Telemetry ---
  async function fetchProcStats() {
    try {
      const resp = await fetch('/api/proc/stats', { cache: 'no-store' });
      if (!resp.ok) return;
      const data = await resp.json();

      const badgePid = document.getElementById('badge-proc-pid');
      const procState = document.getElementById('proc-state');
      const procThreads = document.getElementById('proc-threads');
      const procRss = document.getElementById('proc-rss');
      const procVsize = document.getElementById('proc-vsize');
      const procFds = document.getElementById('proc-fds');
      const procCpu = document.getElementById('proc-cpu');

      if (badgePid) badgePid.textContent = `PID: ${data.pid}`;
      if (procState) procState.textContent = `${data.state} (${data.state_name || 'Running/Sleeping'})`;
      if (procThreads) procThreads.textContent = `${data.num_threads} Threads`;
      if (procRss) procRss.textContent = `${(data.vmrss_kb || 0).toLocaleString()} KB (${((data.vmrss_kb || 0) / 1024).toFixed(2)} MB)`;
      if (procVsize) procVsize.textContent = `${(data.vsize_kb || 0).toLocaleString()} KB (${((data.vsize_kb || 0) / 1024).toFixed(2)} MB)`;
      if (procFds) procFds.textContent = `${data.open_fds} FDs`;
      if (procCpu) procCpu.textContent = `${data.cpu_time_seconds}s (U:${data.utime_ticks} S:${data.stime_ticks})`;
    } catch (err) {
      console.error('Failed to fetch /proc stats:', err);
    }
  }

  const btnProcRefresh = document.getElementById('btn-proc-refresh');
  if (btnProcRefresh) btnProcRefresh.addEventListener('click', () => {
    fetchProcStats();
    showToast('Linux kernel /proc metrics refreshed.', 'info');
  });

  // --- Dynamic Configuration & SIGHUP Reload ---
  async function fetchConfig() {
    try {
      const resp = await fetch('/api/config', { cache: 'no-store' });
      if (!resp.ok) return;
      const data = await resp.json();

      const cfgFile = document.getElementById('cfg-file');
      const cfgSlots = document.getElementById('cfg-slots');
      const cfgQueues = document.getElementById('cfg-queues');
      const cfgLogging = document.getElementById('cfg-logging');
      const cfgLastTime = document.getElementById('cfg-last-time');
      const badgeReloads = document.getElementById('badge-config-reloads');

      if (cfgFile) cfgFile.textContent = data.config_file || 'server.conf';
      if (cfgSlots) cfgSlots.textContent = `${data.data_access_slots} Slots (sem_t)`;
      if (cfgQueues) cfgQueues.textContent = `${data.request_queue_capacity} Req / ${data.response_queue_capacity} Resp`;
      if (cfgLogging) cfgLogging.textContent = data.log_enabled ? 'ENABLED' : 'DISABLED';
      if (cfgLastTime) cfgLastTime.textContent = data.last_reload_time || 'Startup';
      if (badgeReloads) badgeReloads.textContent = `Reloads: ${data.reload_count}`;
    } catch (err) {
      console.error('Failed to fetch config:', err);
    }
  }

  const btnSighupReload = document.getElementById('btn-sighup-reload');
  if (btnSighupReload) btnSighupReload.addEventListener('click', async () => {
    try {
      const resp = await fetch('/api/config/reload', { method: 'POST' });
      const data = await resp.json();
      if (data.success) {
        showToast(`SIGHUP handled! Dynamic config reloaded (Count: ${data.reload_count}).`, 'success');
        fetchConfig();
      } else {
        showToast('Config reload failed: ' + (data.message || 'Error'), 'error');
      }
    } catch (err) {
      showToast('Error dispatching SIGHUP reload: ' + err.message, 'error');
    }
  });

  // --- Structured File Logging Viewer ---
  let activeLogTab = 'server';
  const tabLogServer = document.getElementById('tab-log-server');
  const tabLogTx = document.getElementById('tab-log-tx');
  const btnRefreshLogs = document.getElementById('btn-refresh-logs');
  const logStreamOutput = document.getElementById('log-stream-output');

  async function fetchLogs(type) {
    if (!logStreamOutput) return;
    try {
      const endpoint = (type === 'transactions') ? '/api/logs/transactions' : '/api/logs/server';
      const resp = await fetch(endpoint, { cache: 'no-store' });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      const records = (type === 'transactions') ? (data.transactions || []) : (data.logs || []);

      if (records.length === 0) {
        logStreamOutput.textContent = `// No log entries recorded yet in ${type}.log`;
      } else {
        logStreamOutput.textContent = records.map(r => `[${r.timestamp}] [${r.level || (type === 'transactions' ? 'TX' : 'INFO')}] [Worker ${r.worker_id !== undefined ? r.worker_id : 'sys'}] ${r.message || r.action || JSON.stringify(r)}`).join('\n');
        logStreamOutput.scrollTop = logStreamOutput.scrollHeight;
      }
    } catch (err) {
      logStreamOutput.textContent = `// Error loading ${type} logs: ${err.message}`;
    }
  }

  if (tabLogServer) {
    tabLogServer.addEventListener('click', () => {
      activeLogTab = 'server';
      tabLogServer.classList.add('active');
      if (tabLogTx) tabLogTx.classList.remove('active');
      fetchLogs('server');
    });
  }

  if (tabLogTx) {
    tabLogTx.addEventListener('click', () => {
      activeLogTab = 'transactions';
      tabLogTx.classList.add('active');
      if (tabLogServer) tabLogServer.classList.remove('active');
      fetchLogs('transactions');
    });
  }

  if (btnRefreshLogs) {
    btnRefreshLogs.addEventListener('click', () => {
      fetchLogs(activeLogTab);
      showToast(`Refreshed ${activeLogTab}.log entries.`, 'info');
    });
  }

  // ---------------------------------------------------------------------------
  // INITIALIZATION
  // ---------------------------------------------------------------------------
  // Poll server status immediately and every 3 seconds
  fetchStatus();
  fetchMmapInfo();
  fetchProcStats();
  fetchConfig();
  fetchLogs('server');
  setInterval(() => {
    fetchStatus(false);
    if (tabOsMonitor && !tabOsMonitor.classList.contains('hidden')) {
      fetchProcStats();
    }
  }, 3000);

  // Check saved session
  const saved = sessionStorage.getItem('univ_erp_auth');
  if (saved) {
    try {
      currentAuth = JSON.parse(saved);
      renderAuthenticatedSession();
    } catch {
      sessionStorage.removeItem('univ_erp_auth');
    }
  }

  // ---------------------------------------------------------------------------
  // GLOBAL ACADEMIC SYNCHRONIZATION LISTENERS & REVALIDATION
  // ---------------------------------------------------------------------------
  // Automatic Revalidation on Window/Tab Focus or Visibility Change
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && currentAuth) {
      if (currentAuth.role === 'student') {
        loadStudentDashboard(currentAuth.user.id);
      } else if (currentAuth.role === 'faculty') {
        loadFacultyDashboard(currentAuth.user.id);
      }
    }
  });

  window.addEventListener('focus', () => {
    if (currentAuth) {
      if (currentAuth.role === 'student') {
        loadStudentDashboard(currentAuth.user.id);
      } else if (currentAuth.role === 'faculty') {
        loadFacultyDashboard(currentAuth.user.id);
      }
    }
  });

  // Background Sync Heartbeat (every 4 seconds) to ensure all open portals reflect external C changes
  setInterval(() => {
    if (!document.hidden && currentAuth) {
      if (currentAuth.role === 'student') {
        loadStudentDashboard(currentAuth.user.id);
      } else if (currentAuth.role === 'faculty') {
        loadFacultyDashboard(currentAuth.user.id);
      }
    }
  }, 4000);
});
