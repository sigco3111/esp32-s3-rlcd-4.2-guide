/* RLCD Bring-up Log — static site behaviors
 * 1) 다크/라이트 테마 토글 (localStorage 저장)
 * 2) 목차 스크롤 스파이
 * 3) 코드블록 복사 버튼
 * 4) 읽기 진행률 바
 */
(function () {
  'use strict';

  var THEME_KEY = 'rlcd-blog-theme';

  /* ---------------- 테마 ---------------- */
  var root = document.documentElement;
  var themeToggle = document.getElementById('themeToggle');

  function readStoredTheme() {
    try {
      return window.localStorage.getItem(THEME_KEY);
    } catch (e) {
      return null;
    }
  }

  function storeTheme(theme) {
    try {
      window.localStorage.setItem(THEME_KEY, theme);
    } catch (e) {
      /* private mode 등: 무시 */
    }
  }

  function systemTheme() {
    try {
      if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches) {
        return 'light';
      }
    } catch (e) { /* noop */ }
    return 'dark';
  }

  function applyTheme(theme) {
    root.setAttribute('data-theme', theme);
    if (themeToggle) {
      themeToggle.setAttribute('aria-pressed', theme === 'light' ? 'true' : 'false');
      themeToggle.setAttribute(
        'title',
        theme === 'light' ? '현재 라이트 테마 (누르면 다크로 전환)' : '현재 다크 테마 (누르면 라이트로 전환)'
      );
    }
  }

  var initialTheme = readStoredTheme() || systemTheme();
  applyTheme(initialTheme);

  if (themeToggle) {
    themeToggle.addEventListener('click', function () {
      var next = root.getAttribute('data-theme') === 'light' ? 'dark' : 'light';
      applyTheme(next);
      storeTheme(next);
    });
  }

  /* ---------------- 모바일 목차 토글 ---------------- */
  var tocToggle = document.getElementById('tocToggle');
  var tocNav = document.getElementById('tocNav');

  function closeToc() {
    if (!tocNav) return;
    tocNav.classList.remove('is-open');
    document.body.classList.remove('toc-open');
    if (tocToggle) tocToggle.setAttribute('aria-expanded', 'false');
  }

  if (tocToggle && tocNav) {
    tocToggle.addEventListener('click', function (event) {
      event.stopPropagation();
      var open = tocNav.classList.toggle('is-open');
      document.body.classList.toggle('toc-open', open);
      tocToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    });

    document.addEventListener('click', function (event) {
      if (!tocNav.classList.contains('is-open')) return;
      if (tocNav.contains(event.target) || tocToggle.contains(event.target)) return;
      closeToc();
    });

    document.addEventListener('keydown', function (event) {
      if (event.key === 'Escape') closeToc();
    });
  }

  /* ---------------- 코드블록 복사 버튼 ---------------- */
  function legacyCopy(text) {
    return new Promise(function (resolve, reject) {
      try {
        var ta = document.createElement('textarea');
        ta.value = text;
        ta.setAttribute('readonly', '');
        ta.style.position = 'fixed';
        ta.style.top = '-1000px';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        ta.setSelectionRange(0, ta.value.length);
        var ok = document.execCommand('copy');
        document.body.removeChild(ta);
        ok ? resolve() : reject(new Error('copy failed'));
      } catch (err) {
        reject(err);
      }
    });
  }

  function copyText(text) {
    // Clipboard API 는 권한이 없거나 insecure context 에서 reject 할 수 있다.
    // reject 되더라도 legacy 경로로 한 번 더 시도한다.
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(function () {
        return legacyCopy(text);
      });
    }
    return legacyCopy(text);
  }

  var COPY_ICON =
    '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" ' +
    'stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="9" y="9" width="11" height="11" rx="2"></rect>' +
    '<path d="M5 15H4.8A1.8 1.8 0 013 13.2V4.8A1.8 1.8 0 014.8 3h8.4A1.8 1.8 0 0115 4.8V5"></path></svg>';

  var CHECK_ICON =
    '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" ' +
    'stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M5 12.5l4.5 4.5L19 7.5"></path></svg>';

  var blocks = document.querySelectorAll('.code-block');
  Array.prototype.forEach.call(blocks, function (block) {
    var code = block.querySelector('pre > code');
    if (!code) return;

    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'copy-btn';
    btn.setAttribute('aria-label', '코드 복사');
    btn.innerHTML = COPY_ICON + '<span class="copy-label">복사</span>';

    btn.addEventListener('click', function () {
      var text = code.innerText.replace(/\n$/, '');
      copyText(text).then(
        function () {
          btn.classList.add('is-copied');
          btn.innerHTML = CHECK_ICON + '<span class="copy-label">복사됨</span>';
          window.setTimeout(function () {
            btn.classList.remove('is-copied');
            btn.innerHTML = COPY_ICON + '<span class="copy-label">복사</span>';
          }, 1800);
        },
        function () {
          btn.classList.add('is-error');
          btn.innerHTML = '<span class="copy-label">실패</span>';
          window.setTimeout(function () {
            btn.classList.remove('is-error');
            btn.innerHTML = COPY_ICON + '<span class="copy-label">복사</span>';
          }, 1800);
        }
      );
    });

    var head = block.querySelector('.code-head');
    if (head) {
      head.appendChild(btn);
    } else {
      block.insertBefore(btn, block.firstChild);
    }
  });

  /* ---------------- 목차 스크롤 스파이 ---------------- */
  var tocLinks = document.querySelectorAll('.toc-list a[data-toc]');
  var targets = [];

  Array.prototype.forEach.call(tocLinks, function (link) {
    var id = link.getAttribute('data-toc');
    var target = id ? document.getElementById(id) : null;
    if (target) targets.push({ link: link, el: target });
  });

  // 목차에 섹션(h2)과 하위 제목(h3)이 섞여 있으므로 문서 순서로 정렬한다.
  targets.sort(function (a, b) {
    return a.el.compareDocumentPosition(b.el) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
  });

  function setActive(id) {
    if (!id || id === activeId) return;
    activeId = id;
    Array.prototype.forEach.call(tocLinks, function (link) {
      var isActive = link.getAttribute('data-toc') === id;
      link.classList.toggle('is-active', isActive);
      if (isActive) {
        link.setAttribute('aria-current', 'true');
      } else {
        link.removeAttribute('aria-current');
      }
    });
  }

  /* 스크롤 스파이: 단일 소스(scroll 위치) 기준으로 항상 같은 결과를 낸다.
   * 활성 판정선(activation line) 위에 마지막으로 지나온 섹션을 활성화한다.
   *
   * 갱신은 scroll 이벤트에서 동기적으로 수행한다.
   * - rAF/setTimeout 은 탭이 숨겨져 있으면 발화하지 않거나 크게 지연된다.
   * - scroll 이벤트는 프레임마다 1회로 병합되므로 동기 계산 비용이 크지 않다.
   * - 읽기(getBoundingClientRect)를 모두 먼저 모아 처리하고, 쓰기(클래스 변경)는
   *   마지막에 몰아서 수행해 레이아웃 재계산을 한 번만 유발한다. */
  var ACTIVATION_LINE = 140;
  var activeId = null;

  function computeActive() {
    if (!targets.length) return null;

    var doc = document.documentElement;
    var atBottom = doc.scrollHeight - (window.pageYOffset + window.innerHeight) < 4;
    if (atBottom) return targets[targets.length - 1].el.id;

    var current = targets[0].el.id;
    for (var i = 0; i < targets.length; i++) {
      var top = targets[i].el.getBoundingClientRect().top;
      if (top - ACTIVATION_LINE <= 0) {
        current = targets[i].el.id;
      } else {
        break;
      }
    }
    return current;
  }

  function updateSpy() {
    setActive(computeActive());
  }

  if (targets.length) {
    window.addEventListener('scroll', updateSpy, { passive: true });
    window.addEventListener('resize', updateSpy);
    window.addEventListener('load', updateSpy);
    window.addEventListener('hashchange', updateSpy);
    updateSpy();
  }

  /* ---------------- 읽기 진행률 ---------------- */
  var progressBar = document.getElementById('progressBar');
  var article = document.querySelector('.post');

  function updateProgress() {
    if (!progressBar || !article) return;
    var rect = article.getBoundingClientRect();
    var total = rect.height - window.innerHeight;
    var scrolled = -rect.top;
    var pct = 0;
    if (total > 0) {
      pct = Math.min(100, Math.max(0, (scrolled / total) * 100));
    } else if (scrolled >= 0) {
      pct = 100;
    }
    progressBar.style.width = pct.toFixed(2) + '%';
  }

  window.addEventListener('scroll', updateProgress, { passive: true });
  window.addEventListener('resize', updateProgress);
  updateProgress();
})();
