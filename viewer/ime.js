// Remote keyboard / IME bridge (design/06-protocol.md sections 3.2-3.3), Chrome 89+.
(function (root) {
  'use strict';
  var INPUTMODE = { numeric: 'numeric', decimal: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search', none: 'none', text: 'text' };
  var TYPE_MODE = { number: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search' };

  function RKIme(ta, button, input, sendCtl, isTouch) {
    var textArea = ta, password = document.getElementById('ime-password'), suppressForwarding = false;
    var composing = false, state = { focused: false }, lastSeq = -1, wantShow = false, showTimer = null;
    var seenSeq = {}, pressed = {};
    var keyboardViewportHeight = null;
    var preedit = '', lastCompose = null;
    function sendIme(message) {
      if (state.context != null) message.context = state.context;
      sendCtl(message);
    }
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
    function active(el) { return input.enabled && state.focused && !suppressForwarding && el === ta; }
    function compose(cursor) {
      cursor = Math.max(0, Math.min(preedit.length, cursor));
      if (lastCompose && lastCompose.text === preedit && lastCompose.cursor === cursor) return;
      lastCompose = { text: preedit, cursor: cursor };
      sendIme({ t: 'ime.compose', text: preedit, cursor: cursor });
    }
    function composeAtSelection(el) {
      var start = el.value.lastIndexOf(preedit);
      if (start >= 0 && el.selectionEnd != null) compose(el.selectionEnd - start);
    }
    function releaseKeys() {
      Object.keys(pressed).forEach(function (id) { input.sendKey(pressed[id], 1); });
      pressed = {};
    }
    clear(textArea); if (password) clear(password);
    input.keyTarget = ta;

    function place() {
      var c = root.RKInput.contentRect(document.getElementById('video')), r = state.rect;
      if (!c || !r) return;
      var vp = root.visualViewport;
      var left = vp ? vp.offsetLeft : 0, top = vp ? vp.offsetTop : 0;
      var width = vp ? vp.width : root.innerWidth, height = vp ? vp.height : root.innerHeight;
      ta.style.left = Math.round(Math.max(left, Math.min(left + width - 1, c.left + r.x / 65535 * c.width))) + 'px';
      ta.style.top = Math.round(Math.max(top, Math.min(top + height - 1, c.top + (r.y + r.h) / 65535 * c.height))) + 'px';
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
      if (!input.enabled || !state.focused || (isTouch && state.mode === 'none')) return;
      wantShow = true; place();
      if (document.activeElement !== ta) { ta.focus({ preventScroll: true }); clear(); }
      clearTimeout(showTimer);
      if (isTouch) {
        var h0 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
        if (keyboardViewportHeight == null) keyboardViewportHeight = h0;
        showTimer = setTimeout(function () {
          var h1 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
          if (input.enabled && wantShow && (document.activeElement !== ta || h1 >= h0 - 50)) button.style.display = 'block';
        }, 1000);
      }
    }
    function hide() {
      wantShow = false; keyboardViewportHeight = null;
      clearTimeout(showTimer); button.style.display = 'none';
      suppressForwarding = true;
      releaseKeys(); ta.blur(); clear(); composing = false; forgetForwarded();
      suppressForwarding = false;
    }
    button.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      if (!input.enabled || !state.focused || state.mode === 'none') return;
      button.style.display = 'none';
      // Refocus inside the trusted gesture even if programmatic focus previously succeeded without an OSK.
      var wanted = wantShow; wantShow = false;
      suppressForwarding = true; ta.blur(); suppressForwarding = false;
      ta.focus({ preventScroll: true }); wantShow = wanted;
    });

    this.clearSession = function () {
      hide(); textArea.blur(); if (password) password.blur();
      textArea.value = ''; if (password) password.value = '';
      lastSeq = -1; seenSeq = {}; state.focused = false;
    };
    this.refresh = function () {
      if (state.focused && (!isTouch || wantShow)) show();
    };
    this.onCtl = function (m) {
      if (m.t !== 'ime.state' && m.t !== 'ime.vk') return false;
      // Older mock endpoints share a sequence between state and visibility.
      if (m.seq != null) {
        if (m.seq < lastSeq || seenSeq[m.t] === m.seq) return true;
        lastSeq = m.seq; seenSeq[m.t] = m.seq;
      }
      if (m.t === 'ime.state') {
        var contextChanged = state.context != null && m.context != null && state.context !== m.context;
        var next = m.type === 'password' && password ? password : textArea;
        var wasShowing = wantShow;
        // Cancel the local native preedit silently. Its trailing end event must
        // not commit into the newly focused remote context.
        if (contextChanged && composing) hide();
        if (next !== ta) {
          // Remote focus has already changed: never cancel into the new field.
          hide(); ta.disabled = true; ta = next; ta.disabled = false;
          input.keyTarget = ta; clear();
        }
        state.focused = !!m.focused; state.type = m.type; state.mode = m.mode; state.flags = m.flags; state.rect = m.rect;
        state.reason = m.reason; state.composing = !!m.composing;
        state.context = m.context;
        applyAttrs();
        if (!state.focused || (isTouch && state.mode === 'none')) hide(); else if (!isTouch || wasShowing) show();
        else { if (!composing && ta.value === '') clear(); place(); }
      } else if (m.show || (!isTouch && state.focused)) show(); else hide();
      return true;
    };

    function attach(el) {
      // Both editors use the same handlers; only the selected editor can forward.
      ['keydown', 'keyup'].forEach(function (type, action) {
        el.addEventListener(type, function (e) {
          var id = e.code || e.key;
          if (action) {
            if (pressed[id] && input.enabled && !suppressForwarding) {
              delete pressed[id]; input.sendKey(e, action);
            }
            return;
          }
          if (!active(el) || e.isComposing || e.keyCode === 229 || composing) return;
          forgetForwarded();
          var altGraph = e.getModifierState && e.getModifierState('AltGraph');
          var shortcut = e.metaKey || ((e.ctrlKey || e.altKey) && !altGraph);
          // Let native text/dead keys start IME. Only beforeinput/composition
          // inserts text remotely; forwarding these keydowns would insert twice.
          if (!shortcut && (Array.from(e.key).length === 1 || e.key === 'Dead' || e.key === 'Process' || e.key === 'Unidentified')) return;
          // Paste is read from this editor's input events, not the cloud clipboard.
          if ((e.ctrlKey || e.metaKey) && !e.altKey && e.key.toLowerCase() === 'v') return;
          if (!/^(Shift|Control|Alt|Meta|AltGraph)$/.test(e.key)) e.preventDefault();
          pressed[id] = { code: e.code, key: e.key, keyCode: e.keyCode, location: e.location };
          input.sendKey(e, action);
        });
      });
      el.addEventListener('compositionstart', function () {
        if (active(el)) { forgetForwarded(); composing = true; preedit = ''; lastCompose = null; }
      });
      el.addEventListener('compositionupdate', function (e) {
        if (active(el) && composing) {
          preedit = e.data || '';
          // compositionupdate precedes the DOM edit; input/selectionchange below
          // then publishes the actual UTF-16 selection within the new preedit.
          compose(preedit.length);
        }
      });
      el.addEventListener('compositionend', function (e) {
        if (!active(el) || !composing) {
          if (active(el)) remember(e.data || '', 'composition');
          clear(el); return;
        }
        composing = false;
        if (e.data) sendIme({ t: 'ime.commit', text: e.data }); else sendIme({ t: 'ime.compose', text: '', cursor: 0 });
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
          if (text) sendIme({ t: 'ime.commit', text: text });
        } else if (t === 'insertText' || t === 'insertReplacementText') {
          if (text == null) return;
          if (text) sendIme({ t: 'ime.commit', text: text });
        } else if (t === 'insertLineBreak' || t === 'insertParagraph') sendIme({ t: 'ime.key', key: 'Enter' });
        else if (t === 'deleteContentBackward') sendIme({ t: 'ime.delete', before: 1, after: 0 });
        else if (t === 'deleteContentForward') sendIme({ t: 'ime.delete', before: 0, after: 1 });
        else if (t === 'deleteWordBackward' || t === 'deleteSoftLineBackward') sendIme({ t: 'ime.key', key: 'Backspace', mods: 2 });
        else return;
        remember(text || '', t); e.preventDefault(); clear(el);
      });
      el.addEventListener('input', function (e) {
        if (!active(el)) { clear(el); return; }
        if (composing || e.isComposing) {
          if (composing) composeAtSelection(el);
          return;
        }
        var text = difference(el);
        var alreadySent = forwarded && (e.inputType === forwarded.type || e.data === forwarded.text || text === forwarded.text);
        if (!alreadySent) {
          if (e.inputType === 'deleteContentBackward') sendIme({ t: 'ime.delete', before: 1, after: 0 });
          else if (e.inputType === 'deleteContentForward') sendIme({ t: 'ime.delete', before: 0, after: 1 });
          else if (el.value !== ' ' && text) sendIme({ t: 'ime.commit', text: text });
        }
        forgetForwarded(); clear(el);
      });
      el.addEventListener('blur', function () {
        if (el !== ta) return;
        releaseKeys();
        if (composing && active(el)) sendIme({ t: 'ime.compose', text: '', cursor: 0 });
        composing = false; clear(el); forgetForwarded();
        if (wantShow && input.enabled) sendIme({ t: 'ime.hidden' });
        wantShow = false;
      });
    }
    attach(textArea); if (password) attach(password);
    document.addEventListener('selectionchange', function () {
      if (composing && active(ta) && document.activeElement === ta) composeAtSelection(ta);
    });
    // A subsequent trusted touch can open an OSK blocked at the first remote RTT.
    input.onPointerDown = function (e) {
      if (isTouch && e.pointerType !== 'mouse' && wantShow) show();
      else if (!isTouch && state.focused) show();
    };
    root.addEventListener('resize', place);
    var video = document.getElementById('video');
    video.addEventListener('loadedmetadata', place);
    if (root.visualViewport) {
      root.visualViewport.addEventListener('resize', function () {
        var h = root.visualViewport.height;
        // Android can dismiss its OSK without blurring the focused editor.
        if (isTouch && wantShow && keyboardViewportHeight != null && h > keyboardViewportHeight + 80) {
          if (composing && input.enabled) sendIme({ t: 'ime.compose', text: '', cursor: 0 });
          sendIme({ t: 'ime.hidden' }); hide();
        } else if (isTouch && wantShow) keyboardViewportHeight = Math.min(keyboardViewportHeight == null ? h : keyboardViewportHeight, h);
        place();
      });
      root.visualViewport.addEventListener('scroll', place);
    }
  }
  root.RKIme = RKIme;
})(typeof self !== 'undefined' ? self : this);
