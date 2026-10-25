// Remote keyboard / IME bridge (design/06-protocol.md sections 3.2-3.3), Chrome 89+.
(function (root) {
  'use strict';
  var INPUTMODE = { numeric: 'numeric', decimal: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search', none: 'none', text: 'text' };
  var TYPE_MODE = { number: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search' };

  function RKIme(ta, button, input, sendCtl, isTouch) {
    var textArea = ta, password = document.getElementById('ime-password'), suppressForwarding = false;
    var composing = false, state = { focused: false }, lastSeq = -1, wantShow = false, showTimer = null;
    var forwarded = null, forwardedTimer = null;
    this.state = state;
    // The sentinel makes Android Backspace produce a deletion even without local text context.
    function clear(el) { el = el || ta; el.value = ' '; try { el.setSelectionRange(1, 1); } catch (e) { /* ignore */ } }
    function forgetForwarded() { forwarded = null; clearTimeout(forwardedTimer); }
    function remember(text, type) {
      forgetForwarded(); forwarded = { text: text, type: type };
      // Trailing input/composition events belong to this edit, not to the next user gesture.
      forwardedTimer = setTimeout(forgetForwarded, 0);
    }
    function difference(el) { return el.value.charAt(0) === ' ' ? el.value.slice(1) : el.value; }
    function active(el) { return input.enabled && !suppressForwarding && el === ta; }
    clear(textArea); if (password) clear(password);
    input.keyTarget = ta;

    function place() {
      var c = root.RKInput.contentRect(document.getElementById('video')), r = state.rect;
      if (!c || !r) return;
      ta.style.left = Math.round(c.left + r.x / 65535 * c.width) + 'px';
      ta.style.top = Math.round(c.top + (r.y + r.h) / 65535 * c.height) + 'px';
    }
    function applyAttrs() {
      var mode = state.mode && state.mode !== 'default' ? INPUTMODE[state.mode] : TYPE_MODE[state.type];
      if (mode) ta.setAttribute('inputmode', mode); else ta.removeAttribute('inputmode');
      var f = state.flags | 0;
      ta.setAttribute('autocorrect', f & 2 ? 'off' : 'on');
      ta.setAttribute('autocapitalize', f & 8 ? 'none' : f & 16 ? 'characters' : f & 32 ? 'words' : f & 64 ? 'sentences' : 'off');
      ta.spellcheck = !(f & 4) && state.type !== 'password';
      ta.setAttribute('autocomplete', f & 1 ? 'off' : 'on');
    }
    function show() {
      if (!input.enabled) return;
      wantShow = true; place();
      if (document.activeElement !== ta) { ta.focus({ preventScroll: true }); clear(); }
      clearTimeout(showTimer);
      if (isTouch) {
        var h0 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
        showTimer = setTimeout(function () {
          var h1 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
          if (input.enabled && wantShow && (document.activeElement !== ta || h1 >= h0 - 50)) button.style.display = 'block';
        }, 1000);
      }
    }
    function hide() {
      wantShow = false; clearTimeout(showTimer); button.style.display = 'none';
      suppressForwarding = true;
      ta.blur(); clear(); composing = false; forgetForwarded();
      suppressForwarding = false;
    }
    button.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      if (!input.enabled) return;
      button.style.display = 'none';
      // Refocus inside the trusted gesture even if programmatic focus previously succeeded without an OSK.
      var wanted = wantShow; wantShow = false; ta.blur(); ta.focus({ preventScroll: true }); wantShow = wanted;
    });

    this.clearSession = function () {
      hide(); textArea.blur(); if (password) password.blur();
      textArea.value = ''; if (password) password.value = '';
      lastSeq = -1; state.focused = false;
    };
    this.onCtl = function (m) {
      if (m.t !== 'ime.state' && m.t !== 'ime.vk') return false;
      if (m.seq != null) { if (m.seq <= lastSeq) return true; lastSeq = m.seq; }
      if (m.t === 'ime.state') {
        var next = m.type === 'password' && password ? password : textArea;
        var wasShowing = wantShow;
        if (next !== ta) {
          if (composing && input.enabled) sendCtl({ t: 'ime.compose', text: '', cursor: 0 });
          hide(); ta.disabled = true; ta = next; ta.disabled = false;
          input.keyTarget = ta; clear();
        }
        state.focused = !!m.focused; state.type = m.type; state.mode = m.mode; state.flags = m.flags; state.rect = m.rect;
        applyAttrs();
        if (!state.focused) hide(); else if (!isTouch || wasShowing) show();
        else { if (!composing && ta.value === '') clear(); place(); }
      } else if (m.show) show(); else hide();
      return true;
    };

    function attach(el) {
      // Both editors use the same handlers; only the selected editor can forward.
      ['keydown', 'keyup'].forEach(function (type, action) {
        el.addEventListener(type, function (e) {
          if (!active(el) || e.isComposing || e.keyCode === 229 || composing) return;
          forgetForwarded(); e.preventDefault(); input.sendKey(e, action);
        });
      });
      el.addEventListener('compositionstart', function () { if (active(el)) { forgetForwarded(); composing = true; } });
      el.addEventListener('compositionupdate', function (e) {
        if (active(el)) sendCtl({ t: 'ime.compose', text: e.data || '', cursor: (e.data || '').length });
      });
      el.addEventListener('compositionend', function (e) {
        if (!active(el)) { clear(el); return; }
        composing = false;
        if (e.data) sendCtl({ t: 'ime.commit', text: e.data }); else sendCtl({ t: 'ime.compose', text: '', cursor: 0 });
        remember(e.data || '', 'composition'); clear(el);
      });
      el.addEventListener('beforeinput', function (e) {
        if (!active(el) || composing || e.isComposing) return;
        var t = e.inputType, text = e.data;
        if (forwarded && forwarded.type === 'composition' &&
            (t === 'insertFromComposition' || t === 'insertText') && text === forwarded.text) {
          e.preventDefault(); return;
        }
        forgetForwarded();
        if (t === 'insertFromPaste' || t === 'insertFromDrop') {
          if (text == null && e.dataTransfer) text = e.dataTransfer.getData('text');
          if (text == null) return; // Some browsers expose the inserted text only in input.
          if (text) sendCtl({ t: 'ime.commit', text: text });
        } else if (t === 'insertText' || t === 'insertReplacementText') {
          if (text == null) return;
          if (text) sendCtl({ t: 'ime.commit', text: text });
        } else if (t === 'insertLineBreak' || t === 'insertParagraph') sendCtl({ t: 'ime.key', key: 'Enter' });
        else if (t === 'deleteContentBackward') sendCtl({ t: 'ime.delete', before: 1, after: 0 });
        else if (t === 'deleteContentForward') sendCtl({ t: 'ime.delete', before: 0, after: 1 });
        else if (t === 'deleteWordBackward' || t === 'deleteSoftLineBackward') sendCtl({ t: 'ime.key', key: 'Backspace', mods: 2 });
        else return;
        remember(text || '', t); e.preventDefault(); clear(el);
      });
      el.addEventListener('input', function (e) {
        if (!active(el)) { clear(el); return; }
        if (composing || e.isComposing) return;
        var text = difference(el);
        var alreadySent = forwarded && (e.inputType === forwarded.type || e.data === forwarded.text || text === forwarded.text);
        if (el.value !== ' ' && !alreadySent && text) sendCtl({ t: 'ime.commit', text: text });
        forgetForwarded(); clear(el);
      });
      el.addEventListener('blur', function () {
        if (el !== ta) return;
        if (wantShow && input.enabled) sendCtl({ t: 'ime.hidden' });
        wantShow = false;
      });
    }
    attach(textArea); if (password) attach(password);
    if (root.visualViewport) root.visualViewport.addEventListener('resize', place);
  }
  root.RKIme = RKIme;
})(typeof self !== 'undefined' ? self : this);
