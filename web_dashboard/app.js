/**
 * NEXTHOME — app.js (MQTT Edition + Emergency Lockdown)
 */

const App = (() => {
  'use strict';

  const CFG = {
    brokerHost: localStorage.getItem('brokerHost') || '67c33f1aca2d4e9aad823a04f6ea8563.s1.eu.hivemq.cloud',
    wsPort:     localStorage.getItem('wsPort')     || '8884',
    doorPIN:    localStorage.getItem('doorPIN')    || '1234',
    mqttUser:   localStorage.getItem('mqttUser')   || 'Quang1703',
    mqttPass:   localStorage.getItem('mqttPass')   || 'Passkhongco1',
  };

  const T = {
    CMD_DOOR:     'home/cmd/door',
    CMD_PASS:     'home/cmd/password',
    CMD_FAN:      'home/cmd/fan',
    CMD_LOCKDOWN: 'home/cmd/lockdown',   // ← MỚI
    TEMP_1:       'home/temp/room1',
    TEMP_2:       'home/temp/room2',
    DOOR_EVT:     'home/door/event',
    STATUS:       'home/status',
    ONLINE:       'home/online',
    LOCKDOWN:     'home/lockdown',       // ← MỚI
  };

  const STATE = {
    temp:0, hum:0, temp2:0, hum2:0,
    doorLocked:true, pinBuffer:'',
    fanOn:false, fan2On:false,
    tempHistory:[], tempHistory2:[],
    mqttClient:null, mqttConnected:false,
    guestTimer:null, guestEnd:null,
    _fanCmdTime1: 0,
    _fanCmdTime2: 0,
    // ── Emergency Lockdown ──
    emergencyLock: false,
  };

  /* =========================================================  MQTT  */
  function loadMqttLib(cb) {
    if (window.mqtt) { cb(); return; }
    const s = document.createElement('script');
    s.src = 'https://cdnjs.cloudflare.com/ajax/libs/mqtt/5.0.5/mqtt.min.js';
    s.onload  = cb;
    s.onerror = () => { log('❌ mqtt.js không tải được — Demo mode','warn'); startDemoMode(); };
    document.head.appendChild(s);
  }

  function mqttConnect() {
    const url = `wss://${CFG.brokerHost}:${CFG.wsPort}/mqtt`;
    log(`📡 MQTT → ${url}`, 'info');
    const opts = {
      clientId: 'nexthome_' + Math.random().toString(16).slice(2,8),
      clean: true, reconnectPeriod: 4000, connectTimeout: 8000,
    };
    if (CFG.mqttUser) { opts.username = CFG.mqttUser; opts.password = CFG.mqttPass; }

    const client = mqtt.connect(url, opts);
    STATE.mqttClient = client;

    client.on('connect', () => {
      STATE.mqttConnected = true;
      updateMQTTStatus(true);
      log('✓ MQTT broker kết nối OK','ok');
      toast('✅ Kết nối MQTT broker!','ok');
      [T.TEMP_1, T.TEMP_2, T.DOOR_EVT, T.STATUS, T.ONLINE, T.LOCKDOWN].forEach(t => client.subscribe(t, {qos:1}));
    });

    client.on('message', (topic, payload) => {
      try { handleMsg(topic, JSON.parse(payload.toString())); }
      catch { handleMsg(topic, {raw: payload.toString()}); }
    });

    client.on('error',     (e) => { STATE.mqttConnected=false; updateMQTTStatus(false); log('⚠ MQTT: '+e.message,'warn'); });
    client.on('close',     ()  => { STATE.mqttConnected=false; updateMQTTStatus(false); log('ℹ MQTT ngắt — đang thử lại...','info'); });
    client.on('reconnect', ()  => log('🔄 MQTT kết nối lại...','info'));
  }

  function pub(topic, payload) {
    const msg = JSON.stringify(payload);
    if (STATE.mqttClient && STATE.mqttConnected) {
      STATE.mqttClient.publish(topic, msg, {qos:1});
    }
    log(`📤 ${topic.split('/').pop()}: ${msg}`, 'info');
  }

  /* =========================================================  MESSAGE HANDLER  */
  function handleMsg(topic, msg) {

    if (topic === T.TEMP_1) {
      STATE.temp = parseFloat(msg.temp) || STATE.temp;
      STATE.hum  = parseFloat(msg.humi) || STATE.hum;
      STATE.tempHistory.push(STATE.temp);
      if (STATE.tempHistory.length > 20) STATE.tempHistory.shift();
      if (msg.fan !== undefined && Date.now() - STATE._fanCmdTime1 > 2000) {
        const fanOn = msg.fan === 'on';
        STATE.fanOn = fanOn;
        const tg = document.getElementById('fan-toggle'); if (tg) tg.checked = fanOn;
        setFanBlades(1, fanOn);
      }
      updateSensorUI();

    } else if (topic === T.TEMP_2) {
      STATE.temp2 = parseFloat(msg.temp) || STATE.temp2;
      STATE.hum2  = parseFloat(msg.humi) || STATE.hum2;
      STATE.tempHistory2.push(STATE.temp2);
      if (STATE.tempHistory2.length > 20) STATE.tempHistory2.shift();
      setEl('climate-temp2', STATE.temp2.toFixed(1));
      setEl('climate-hum2',  STATE.hum2.toFixed(0) + '%');
      setEl('climate-feel2', heatIndex(STATE.temp2, STATE.hum2).toFixed(1) + '°');
      const hi2 = heatIndex(STATE.temp2, STATE.hum2);
      const ci2 = document.getElementById('climate-index2');
      if (ci2) ci2.textContent = hi2>39?'Nguy hiểm':hi2>32?'Rất nóng':hi2>27?'Nóng':'Bình thường';
      if (msg.fan !== undefined && Date.now() - STATE._fanCmdTime2 > 2000) {
        const fan2On = msg.fan === 'on';
        STATE.fan2On = fan2On;
        const tg2 = document.getElementById('fan2-toggle'); if (tg2) tg2.checked = fan2On;
        setFanBlades(2, fan2On);
      }
      updateSparkline2();

    } else if (topic === T.DOOR_EVT) {
      handleDoorEvent(msg);

    } else if (topic === T.STATUS) {
      syncStatus(msg);

    } else if (topic === T.ONLINE) {
      log('🟢 ESP32 ONLINE','ok'); toast('🟢 ESP32 kết nối!','ok');

    // ── MỚI: Nhận trạng thái lockdown từ ESP32 ──
    } else if (topic === T.LOCKDOWN) {
      const active = msg.active === true || msg.active === 'true';
      if (STATE.emergencyLock !== active) {
        STATE.emergencyLock = active;
        updateLockdownUI();
        if (active) {
          log('🚨 EMERGENCY LOCKDOWN BẬT bởi ESP32!', 'err');
          toast('🚨 Chế độ khóa khẩn cấp đã BẬT!', 'err');
        } else {
          log('✅ Emergency Lockdown đã tắt.', 'ok');
          toast('✅ Lockdown đã được gỡ bỏ!', 'ok');
        }
      }
    }
  }

  function handleDoorEvent(msg) {
    const granted = msg.granted;
    const type    = msg.type    || '';
    const detail  = msg.detail  || '';

    if (type === 'lockdown') {
      // Bị chặn bởi lockdown
      log(`⛔ Bị chặn: ${detail}`, 'err');
      toast('⛔ Bị chặn — Emergency Lockdown đang hoạt động!', 'err');
      return;
    }

    if (type === 'door' && granted) {
      STATE.doorLocked = false; updateDoorUI();
      log(`🔓 Cửa mở — ${detail}`, 'ok');
      toast(`🔓 Cửa mở (${detail})`, 'ok');
    } else if (detail === 'lock_command' || detail === 'auto_locked') {
      STATE.doorLocked = true; updateDoorUI();
      log('🔒 Cửa khóa', 'info');
    } else if (type === 'keypad' && !granted) {
      toast('❌ Mật khẩu sai!', 'err');
    } else if (type === 'rfid' && !granted) {
      toast(`❌ Thẻ không hợp lệ: ${detail.replace('wrong_card:','')}`, 'err');
    } else if (type === 'system') {
      log(`ℹ System: ${detail}`, 'info');
      if (detail === 'password_changed') toast('🔑 Mật khẩu đã đổi', 'ok');
    }
  }

  function syncStatus(msg) {
    if (msg.door !== undefined) {
      STATE.doorLocked = (msg.door === 'locked');
      updateDoorUI();
    }
    if (msg.temp1 !== undefined) {
      STATE.temp = parseFloat(msg.temp1) || STATE.temp;
      updateSensorUI();
    }
    if (msg.temp2 !== undefined) {
      STATE.temp2 = parseFloat(msg.temp2) || STATE.temp2;
    }
    if (msg.fan1 !== undefined) {
      STATE.fanOn = (msg.fan1 === 'on');
      const tg = document.getElementById('fan-toggle'); if (tg) tg.checked = STATE.fanOn;
      setFanBlades(1, STATE.fanOn);
    }
    if (msg.fan2 !== undefined) {
      STATE.fan2On = (msg.fan2 === 'on');
      const tg2 = document.getElementById('fan2-toggle'); if (tg2) tg2.checked = STATE.fan2On;
      setFanBlades(2, STATE.fan2On);
    }
    if (msg.ip) {
      const chip = document.querySelector('.chip--blue');
      if (chip) chip.innerHTML = `<span>📡</span> ${msg.ip}`;
    }
    // ── Đồng bộ lockdown từ status ──
    if (msg.lockdown !== undefined) {
      const ld = msg.lockdown === true || msg.lockdown === 'true';
      if (STATE.emergencyLock !== ld) {
        STATE.emergencyLock = ld;
        updateLockdownUI();
      }
    }
    log(`📊 Sync ESP32 — cửa:${msg.door} quạt1:${msg.fan1} lockdown:${msg.lockdown}`, 'ok');
  }

  function updateMQTTStatus(ok) {
    const el = document.getElementById('ws-status'); if (!el) return;
    el.innerHTML = ok
      ? '<span class="chip-dot"></span> MQTT Connected'
      : '<span style="color:var(--red)">⚠ MQTT Offline</span>';
    el.className = ok ? 'chip chip--green' : 'chip';
  }

  /* =========================================================  EMERGENCY LOCKDOWN UI  */
  function updateLockdownUI() {
    const btn    = document.getElementById('lockdown-btn');
    const badge  = document.getElementById('lockdown-badge');
    const overlay= document.getElementById('lockdown-overlay');
    const body   = document.body;
    const active = STATE.emergencyLock;

    if (btn) {
      btn.classList.toggle('lockdown-active', active);
      btn.querySelector('.ld-icon').textContent = active ? '🔴' : '🔴';
      btn.querySelector('.ld-text').textContent = active ? 'BỎ KHÓA KHẨN' : 'KHÓA KHẨN CẤP';
    }
    if (badge) {
      badge.style.display = active ? 'flex' : 'none';
    }
    if (overlay) {
      overlay.classList.toggle('active', active);
    }
    // Khóa các nút mở cửa trên giao diện
    const unlockBtns = document.querySelectorAll('.unlock-btn, .num-btn.confirm');
    unlockBtns.forEach(b => {
      b.disabled = active;
      b.style.opacity = active ? '0.35' : '1';
      b.style.cursor  = active ? 'not-allowed' : 'pointer';
    });
    // Numpad bị mờ toàn bộ
    const numpad = document.querySelector('.numpad');
    if (numpad) numpad.style.pointerEvents = active ? 'none' : '';

    // Cập nhật door status
    updateDoorUI();
  }

  /* =========================================================  TOGGLE LOCKDOWN  */
  function toggleEmergencyLock() {
    const newState = !STATE.emergencyLock;

    if (newState) {
      // Bật — hiện dialog xác nhận
      showLockdownConfirm(() => {
        STATE.emergencyLock = true;
        pub(T.CMD_LOCKDOWN, {active: true});
        updateLockdownUI();
        log('🚨 Web bật Emergency Lockdown!', 'err');
        toast('🚨 Đã bật khóa khẩn cấp — mọi phương thức mở cửa bị chặn!', 'err');
      });
    } else {
      // Tắt — xác nhận đơn giản hơn
      showUnlockConfirm(() => {
        STATE.emergencyLock = false;
        pub(T.CMD_LOCKDOWN, {active: false});
        updateLockdownUI();
        log('✅ Web tắt Emergency Lockdown.', 'ok');
        toast('✅ Đã gỡ khóa khẩn — hệ thống bình thường trở lại.', 'ok');
      });
    }
  }

  function showLockdownConfirm(onConfirm) {
    const modal = document.getElementById('lockdown-confirm-modal');
    if (!modal) { onConfirm(); return; }
    modal.classList.add('open');
    document.getElementById('lockdown-confirm-yes').onclick = () => {
      modal.classList.remove('open');
      onConfirm();
    };
    document.getElementById('lockdown-confirm-no').onclick = () => {
      modal.classList.remove('open');
    };
  }

  function showUnlockConfirm(onConfirm) {
    const modal = document.getElementById('unlock-confirm-modal');
    if (!modal) { onConfirm(); return; }
    modal.classList.add('open');
    document.getElementById('unlock-confirm-yes').onclick = () => {
      modal.classList.remove('open');
      onConfirm();
    };
    document.getElementById('unlock-confirm-no').onclick = () => {
      modal.classList.remove('open');
    };
  }

  /* =========================================================  SENSORS  */
  function updateSensorUI() {
    setEl('temp-display', STATE.temp.toFixed(1) + '°C');
    setEl('hum-display',  STATE.hum.toFixed(0) + '%');
    setEl('climate-temp', STATE.temp.toFixed(1));
    setEl('climate-hum',  STATE.hum.toFixed(0) + '%');
    setEl('climate-feel', heatIndex(STATE.temp, STATE.hum).toFixed(1) + '°');

    const hi = heatIndex(STATE.temp, STATE.hum);
    const ci = document.getElementById('climate-index');
    if (ci) {
      ci.textContent = hi<27?'Bình thường':hi<32?'Nóng':hi<39?'Rất nóng':'Nguy hiểm';
      ci.style.color = hi<27?'var(--green)':hi<32?'var(--yellow)':'var(--red)';
    }

    const tb = document.getElementById('temp-bar');
    if (tb) tb.style.width = Math.min(100, Math.max(0, ((STATE.temp-15)/25*100))).toFixed(0) + '%';
    const hb = document.getElementById('hum-bar');
    if (hb) hb.style.width = STATE.hum + '%';

    const act = [STATE.fanOn, STATE.fan2On].filter(Boolean).length;
    setEl('device-active', act + '/7');
    setEl('power-display', (0.3 + (STATE.fanOn?0.05:0) + (STATE.fan2On?0.05:0)).toFixed(1) + ' kW');

    updateGauge(STATE.temp);
    setEl('home-gauge-text', STATE.temp.toFixed(0) + '°');
    renderSparkline();
  }

  function heatIndex(T, H) {
    return +(T + 0.33*(H/100*6.105*Math.exp(17.27*T/(237.7+T))) - 4).toFixed(1);
  }

  function renderSparkline() {
    const c = document.getElementById('temp-sparkline');
    if (!c || STATE.tempHistory.length < 2) return;
    const mn = Math.min(...STATE.tempHistory), mx = Math.max(...STATE.tempHistory) || mn+1;
    c.innerHTML = STATE.tempHistory.map(v =>
      `<div class="spark-bar" style="height:${Math.max(4,((v-mn)/(mx-mn+.001)*36)).toFixed(0)}px" title="${v}°C"></div>`
    ).join('');
  }

  function updateSparkline2() {
    const c = document.getElementById('temp-sparkline2');
    if (!c || STATE.tempHistory2.length < 2) return;
    const mn = Math.min(...STATE.tempHistory2), mx = Math.max(...STATE.tempHistory2) || mn+1;
    c.innerHTML = STATE.tempHistory2.map(v =>
      `<div class="spark-bar" style="height:${Math.max(4,((v-mn)/(mx-mn+.001)*36)).toFixed(0)}px" title="${v}°C"></div>`
    ).join('');
  }

  function updateGauge(temp) {
    const c = document.getElementById('gauge-circle'); if (!c) return;
    c.setAttribute('stroke-dashoffset', (201 - Math.min(1, Math.max(0, (temp-16)/22))*201).toFixed(1));
  }

  /* =========================================================  DEMO  */
  function startDemoMode() {
    log('▶ Demo mode (không có broker)', 'warn');
    STATE.temp = 28; STATE.hum = 65;
    function sim() {
      STATE.temp = +(Math.max(18, Math.min(38, STATE.temp + (Math.random()-.48)*.3))).toFixed(1);
      STATE.hum  = Math.round(Math.max(30, Math.min(95, STATE.hum + (Math.random()-.48))));
      STATE.temp2 = +(STATE.temp + (Math.random()-.5)).toFixed(1);
      STATE.hum2  = Math.round(STATE.hum + (Math.random()-.5)*3);
      STATE.tempHistory.push(STATE.temp);  if (STATE.tempHistory.length>20)  STATE.tempHistory.shift();
      STATE.tempHistory2.push(STATE.temp2); if (STATE.tempHistory2.length>20) STATE.tempHistory2.shift();
      updateSensorUI();
      setEl('climate-temp2', STATE.temp2.toFixed(1));
      setEl('climate-hum2',  STATE.hum2 + '%');
    }
    sim(); setInterval(sim, 4000);
  }

  /* =========================================================  CLOCK  */
  function startClock() {
    function tick() {
      const n = new Date(), p = v => String(v).padStart(2,'0');
      const cl = document.getElementById('clock');
      if (cl) cl.textContent = `${p(n.getHours())}:${p(n.getMinutes())}:${p(n.getSeconds())}`;
      const dl = document.getElementById('date-display');
      if (dl) { const D=['CN','T2','T3','T4','T5','T6','T7']; dl.textContent=`${D[n.getDay()]} ${p(n.getDate())}/${p(n.getMonth()+1)}/${n.getFullYear()}`; }
    }
    tick(); setInterval(tick, 1000);
  }

  /* =========================================================  DOOR  */
  function updateDoorUI() {
    const lk = STATE.doorLocked || STATE.emergencyLock;
    toggleClass('door-big',       'open', !lk);
    toggleClass('door-panel-home','open', !lk);
    setEl('lock-icon', STATE.emergencyLock ? '🚨' : (lk ? '🔒' : '🔓'));
    const sp = document.getElementById('door-status-pill');
    if (sp) {
      if (STATE.emergencyLock) {
        sp.textContent = 'EMERGENCY LOCK';
        sp.className = 'status-pill emergency';
      } else {
        sp.textContent = lk ? 'LOCKED' : 'UNLOCKED';
        sp.className = 'status-pill' + (lk ? '' : ' open');
      }
    }
    const bh = document.getElementById('door-badge-home');
    if (bh) {
      if (STATE.emergencyLock) {
        bh.textContent = '🚨 Khóa khẩn';
        bh.className = 'status-badge emergency-badge';
      } else {
        bh.textContent = lk ? '🔒 Khóa' : '🔓 Mở';
        bh.className = 'status-badge' + (lk ? '' : ' green');
      }
    }
    const la = document.getElementById('door-last-action');
    if (la) { const n=new Date(); la.textContent=`Lần cuối: ${n.getHours()}:${String(n.getMinutes()).padStart(2,'0')}`; }
  }

  function pinInput(d) {
    if (STATE.emergencyLock) { toast('⛔ Bị chặn — Emergency Lockdown!', 'err'); return; }
    if (STATE.pinBuffer.length >= 10) return;
    STATE.pinBuffer += d;
    updatePinDisplay();
  }
  function pinClear()  { STATE.pinBuffer = STATE.pinBuffer.slice(0,-1); updatePinDisplay(); }
  function pinConfirm() {
    if (STATE.emergencyLock) {
      toast('⛔ Bị chặn — Emergency Lockdown!', 'err');
      STATE.pinBuffer = ''; updatePinDisplay(); return;
    }
    const pd = document.getElementById('pin-display');
    if (STATE.pinBuffer === CFG.doorPIN) {
      pd && pd.classList.add('success');
      pub(T.CMD_DOOR, {action:'unlock'});
      STATE.doorLocked = false; updateDoorUI();
      toast('🔓 Mở bằng PIN', 'ok'); log('✓ PIN đúng', 'ok');
      STATE.pinBuffer = '';
      setTimeout(() => pd && pd.classList.remove('success'), 1500);
    } else {
      pd && pd.classList.add('error');
      toast('❌ Mã PIN sai!', 'err'); STATE.pinBuffer = '';
      setTimeout(() => { pd && pd.classList.remove('error'); updatePinDisplay(); }, 800);
    }
    updatePinDisplay();
  }
  function updatePinDisplay() {
    const pd = document.getElementById('pin-display'); if (!pd) return;
    const filled = '●'.repeat(STATE.pinBuffer.length);
    const empty  = '_ '.repeat(Math.max(0, 4 - STATE.pinBuffer.length)).trim();
    pd.textContent = (filled + (empty ? ' ' + empty : '')).trim() || '_ _ _ _';
  }

  function simulateFingerprint() {
    toast('ℹ Vân tay chưa được hỗ trợ trong phiên bản này', 'info');
  }
  function addFingerprint() {
    toast('ℹ Tính năng vân tay chưa có trong firmware', 'info');
  }

  function remoteUnlock() {
    if (STATE.emergencyLock) {
      toast('⛔ Bị chặn — Emergency Lockdown đang hoạt động!', 'err');
      log('⛔ Web từ chối mở cửa — Lockdown đang bật', 'err');
      return;
    }
    pub(T.CMD_DOOR, {action:'unlock'});
    toast('📡 Gửi lệnh mở cửa...', 'info');
  }
  function remoteLock() {
    pub(T.CMD_DOOR, {action:'lock'});
    toast('📡 Gửi lệnh khóa cửa...', 'info');
    if (STATE.guestTimer) { clearInterval(STATE.guestTimer); STATE.guestTimer = null; }
    const gt = document.getElementById('guest-timer'); if (gt) gt.style.display = 'none';
  }
  function guestAccess(min) {
    if (STATE.emergencyLock) {
      toast('⛔ Bị chặn — Emergency Lockdown đang hoạt động!', 'err');
      return;
    }
    remoteUnlock();
    if (STATE.guestTimer) clearInterval(STATE.guestTimer);
    if (min > 0) {
      STATE.guestEnd = Date.now() + min * 60000;
      const gt = document.getElementById('guest-timer'), gc = document.getElementById('guest-countdown');
      if (gt) gt.style.display = 'block';
      STATE.guestTimer = setInterval(() => {
        const r = STATE.guestEnd - Date.now();
        if (r <= 0) { clearInterval(STATE.guestTimer); remoteLock(); toast('⏱ Hết giờ khách','warn'); return; }
        const m = Math.floor(r/60000), s = Math.floor((r%60000)/1000);
        if (gc) gc.textContent = `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
      }, 1000);
    }
    toast(`👥 Cửa mở cho khách${min>0?' — '+min+'ph':''}`, 'ok');
  }

  /* =========================================================  CLIMATE / FAN  */
  function toggleFan(n, on) {
    if (n === 1) { STATE.fanOn  = on; STATE._fanCmdTime1 = Date.now(); }
    else         { STATE.fan2On = on; STATE._fanCmdTime2 = Date.now(); }
    pub(T.CMD_FAN, {room: n, action: on ? 'on' : 'off'});
    setFanBlades(n, on, 100);
    const sliderId = n === 2 ? 'fan2-speed' : 'fan-speed';
    const labelId  = n === 2 ? 'fan2-speed-val' : 'fan-speed-val';
    const slider = document.getElementById(sliderId);
    if (slider) slider.value = on ? 100 : 0;
    setEl(labelId, on ? '100%' : '0%');
    toast(on ? `🌀 Bật quạt #${n}` : `⭕ Tắt quạt #${n}`, on ? 'ok' : 'info');
    log(`Quạt #${n}: ${on?'BẬT':'TẮT'}`, on ? 'ok' : 'info');
  }

  function setFanSpeed(n, v) {
    v = parseInt(v);
    const on = v > 0;
    if (n === 2) {
      STATE.fan2On = on; STATE._fanCmdTime2 = Date.now();
      setEl('fan2-speed-val', v + '%');
      const s = document.getElementById('fan2-speed'); if (s) s.value = v;
      setFanBlades(2, on, v);
      const tg = document.getElementById('fan2-toggle'); if (tg) tg.checked = on;
      pub(T.CMD_FAN, {room: 2, action: on ? 'on' : 'off'});
    } else {
      STATE.fanOn = on; STATE._fanCmdTime1 = Date.now();
      setEl('fan-speed-val', v + '%');
      const s = document.getElementById('fan-speed'); if (s) s.value = v;
      setFanBlades(1, on, v);
      const tg = document.getElementById('fan-toggle'); if (tg) tg.checked = on;
      pub(T.CMD_FAN, {room: 1, action: on ? 'on' : 'off'});
    }
  }

  function setFanPreset(n, v) { setFanSpeed(n, v); }

  function setFanBlades(n, on, speedPct) {
    const id = n === 2 ? 'fan2-blades' : 'fan-blades';
    const bl = document.getElementById(id);
    if (!bl) return;
    if (!on) { bl.style.animation = 'none'; return; }
    const pct = (speedPct !== undefined) ? parseInt(speedPct) : 100;
    const dur  = (0.3 + (1 - pct/100) * 1.7).toFixed(2) + 's';
    bl.style.animation = `fan-spin ${dur} linear infinite`;
  }

  function changePassword(newPass) {
    if (newPass.length < 4 || newPass.length > 10) {
      toast('⚠ Mật khẩu phải 4-10 ký tự', 'warn'); return;
    }
    pub(T.CMD_PASS, {new: newPass});
    toast('🔑 Đã gửi lệnh đổi mật khẩu', 'ok');
  }

  /* =========================================================  TABS  */
  function switchTab(btn) {
    document.querySelectorAll('.nav-pill').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    document.querySelectorAll('.tab-section').forEach(s => s.classList.remove('active'));
    const s = document.getElementById('tab-' + btn.dataset.tab); if (s) s.classList.add('active');
  }
  function switchTabById(tab) { const b = document.querySelector(`[data-tab="${tab}"]`); if (b) switchTab(b); }

  /* =========================================================  CONFIG  */
  function openModal()  { document.getElementById('config-modal').classList.add('open'); }
  function closeModal() { document.getElementById('config-modal').classList.remove('open'); }
  function saveConfig() {
    CFG.brokerHost = document.getElementById('esp-ip').value.trim();
    CFG.mqttUser   = document.getElementById('mqtt-user').value.trim();
    CFG.mqttPass   = document.getElementById('mqtt-pass').value.trim();
    CFG.wsPort     = document.getElementById('esp-port').value.trim();
    CFG.doorPIN    = document.getElementById('door-pin-conf').value.trim();
    localStorage.setItem('brokerHost', CFG.brokerHost);
    localStorage.setItem('wsPort',     CFG.wsPort);
    localStorage.setItem('doorPIN',    CFG.doorPIN);
    localStorage.setItem('mqttUser',   CFG.mqttUser);
    localStorage.setItem('mqttPass',   CFG.mqttPass);
    closeModal();
    toast('💾 Đã lưu — kết nối lại MQTT', 'ok');
    if (STATE.mqttClient) STATE.mqttClient.end();
    setTimeout(mqttConnect, 600);
  }

  /* =========================================================  HELPERS  */
  function toast(msg, type='info') {
    const c = document.getElementById('toast-container'); if (!c) return;
    const t = document.createElement('div'); t.className = `toast ${type}`; t.textContent = msg;
    c.appendChild(t); setTimeout(() => t.remove(), 3000);
  }
  function log(msg, type='info') {
    const l = document.getElementById('log-list'); if (!l) return;
    const r = document.createElement('div'); r.className = `log-row ${type}`; r.textContent = msg;
    l.insertBefore(r, l.firstChild); if (l.children.length > 14) l.lastChild.remove();
  }
  function setEl(id, val)       { const e = document.getElementById(id); if (e) e.textContent = val; }
  function toggleClass(id, cls, cond) { const e = document.getElementById(id); if (e) e.classList.toggle(cls, cond); }

  /* =========================================================  INIT  */
  function init() {
    startClock();

    const ipEl = document.getElementById('esp-ip');    if (ipEl) ipEl.value = CFG.brokerHost;
    const uEl  = document.getElementById('mqtt-user'); if (uEl)  uEl.value  = CFG.mqttUser;
    const pwEl = document.getElementById('mqtt-pass'); if (pwEl) pwEl.value = CFG.mqttPass;
    const pEl  = document.getElementById('esp-port');  if (pEl)  pEl.value  = CFG.wsPort;

    const badge2 = document.querySelector('#tab-climate .sensor-card:nth-child(2) .sensor-badge');
    if (badge2) badge2.textContent = 'GPIO16';

    log('✓ NEXTHOME khởi động', 'ok');
    log(`📡 MQTT: ${CFG.brokerHost}:${CFG.wsPort}`, 'info');
    log('🔴 Emergency Lockdown: SẴN SÀNG', 'info');

    loadMqttLib(() => {
      mqttConnect();
      setTimeout(() => { if (!STATE.mqttConnected) startDemoMode(); }, 5000);
    });

    // Gắn nút lockdown
    const ldBtn = document.getElementById('lockdown-btn');
    if (ldBtn) ldBtn.addEventListener('click', toggleEmergencyLock);
  }

  return {
    init, switchTab, switchTabById,
    pinInput, pinClear, pinConfirm,
    simulateFingerprint, addFingerprint,
    remoteUnlock, remoteLock, guestAccess,
    toggleFan, setFanSpeed, setFanPreset,
    changePassword,
    openModal, closeModal, saveConfig,
    toggleEmergencyLock,
  };
})();

document.addEventListener('DOMContentLoaded', () => App.init());