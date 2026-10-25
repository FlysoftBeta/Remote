// Remote on-screen keyboard / IME bridge (design/06-protocol.md sections 3.2-3.3).
// A hidden textarea is focused when the remote asks for the keyboard (ime.vk show); text, composition and
// deletions typed into it are forwarded as ime.* messages and the textarea is cleared after each edit.
(function (root) {
  'use strict';
  var INPUTMODE = { numeric: 'numeric', decimal: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search', none: 'none', text: 'text' };
  var TYPE_MODE = { number: 'decimal', tel: 'tel', url: 'url', email: 'email', search: 'search' };

  function RKIme(ta, button, input, sendCtl, isTouch) {
    var self = this, composing = false, state = { focused: false }, lastSeq = -1, wantShow = false, showTimer = null;
    this.state = state;
    // A one-space sentinel keeps Backspace producing deleteContentBackward on Android soft keyboards.
    function clear() { ta.value = ' '; try { ta.setSelectionRange(1, 1); } catch (e) { /* ignore */ } }
    clear();
    input.keyTarget = ta;

    function place() {
      // Put the (invisible) textarea over the remote caret so OS candidate windows/magnifiers appear near it.
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
      ta.setAttribute('autocomplete', state.type === 'password' ? 'current-password' : 'off');
    }
    function show() {
      wantShow = true;
      place();
      if (document.activeElement !== ta) { ta.focus({ preventScroll: true }); clear(); }
      // Mobile browsers open the soft keyboard on programmatic focus only within user activation. If it did not
      // open (no focus, or the visual viewport did not shrink), offer a button so the user can open it in a gesture.
      clearTimeout(showTimer);
      if (isTouch) {
        var h0 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
        showTimer = setTimeout(function () {
          var h1 = root.visualViewport ? root.visualViewport.height : root.innerHeight;
          if (wantShow && (document.activeElement !== ta || h1 >= h0 - 50)) button.style.display = 'block';
        }, 1000);
      }
    }
    function hide() {
      wantShow = false; clearTimeout(showTimer); button.style.display = 'none';
      if (document.activeElement === ta) ta.blur();
      clear(); composing = false;
    }
    button.addEventListener('pointerdown', function (e) { e.preventDefault(); button.style.display = 'none'; ta.focus({ preventScroll: true }); });

    this.onCtl = function (m) {
      if (m.t !== 'ime.state' && m.t !== 'ime.vk') return false;
      if (m.seq != null) { if (m.seq <= lastSeq) return true; lastSeq = m.seq; }
      if (m.t === 'ime.state') {
        state.focused = !!m.focused; state.type = m.type; state.mode = m.mode; state.flags = m.flags; state.rect = m.rect;
        applyAttrs();
        if (!state.focused) hide(); else if (!isTouch) show(); // desktop: focus so local IMEs (CJK) compose into it
        else place();
      } else if (m.show) show(); else hide();
      return true;
    };

    // Physical keys while the textarea has focus: forward as KEY and block local text insertion.
    ta.addEventListener('keydown', function (e) {
      if (e.isComposing || e.keyCode === 229 || composing) return;
      e.preventDefault(); input.sendKey(e, 0);
    });
    ta.addEventListener('keyup', function (e) {
      if (e.isComposing || e.keyCode === 229 || composing) return;
      e.preventDefault(); input.sendKey(e, 1);
    });
    ta.addEventListener('compositionstart', function () { composing = true; });
    ta.addEventListener('compositionupdate', function (e) { sendCtl({ t: 'ime.compose', text: e.data || '', cursor: (e.data || '').length }); });
    ta.addEventListener('compositionend', function (e) {
      composing = false;
      if (e.data) sendCtl({ t: 'ime.commit', text: e.data }); else sendCtl({ t: 'ime.compose', text: '', cursor: 0 });
      clear();
    });
    ta.addEventListener('beforeinput', function (e) {
      // Soft keyboards (keyCode 229) deliver edits here. Composition-related types are handled by composition events.
      if (composing || e.isComposing) return;
      var t = e.inputType;
      if (t === 'insertText' || t === 'insertReplacementText') { if (e.data) sendCtl({ t: 'ime.commit', text: e.data }); }
      else if (t === 'insertLineBreak' || t === 'insertParagraph') sendCtl({ t: 'ime.key', key: 'Enter' });
      else if (t === 'deleteContentBackward') sendCtl({ t: 'ime.delete', before: 1, after: 0 });
      else if (t === 'deleteContentForward') sendCtl({ t: 'ime.delete', before: 0, after: 1 });
      else if (t === 'deleteWordBackward' || t === 'deleteSoftLineBackward') sendCtl({ t: 'ime.key', key: 'Backspace' });
      else return;
      e.preventDefault();
    });
    ta.addEventListener('input', function () { if (!composing) clear(); });
    ta.addEventListener('blur', function () { if (wantShow) sendCtl({ t: 'ime.hidden' }); wantShow = false; });
    if (root.visualViewport) root.visualViewport.addEventListener('resize', place);
  }
  root.RKIme = RKIme;
})(typeof self !== 'undefined' ? self : this);
