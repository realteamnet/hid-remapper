(() => {
    const groups = {
        reg: ['store', 'recall'],
        logic: ['eq', 'gt', 'lt', 'not', 'bitwise_or', 'bitwise_and', 'bitwise_not', 'ifte'],
        math: ['add', 'mul', 'sub', 'div', 'mod', 'abs', 'min', 'max', 'clamp', 'sqrt', 'sin', 'cos', 'atan2',
            'round', 'sign', 'relu', 'scaling', 'deadzone', 'deadzone2', 'auto_repeat'],
        input: ['input_state', 'input_state_binary', 'input_state_fp32', 'input_state_scaled', 'prev_input_state',
            'prev_input_state_binary', 'prev_input_state_fp32', 'prev_input_state_scaled', 'layer_state',
            'sticky_state', 'tap_state', 'hold_state', 'time', 'time_sec', 'port', 'plugged_in', 'dpad'],
        stack: ['dup', 'swap'],
        dbg: ['debug', 'monitor', 'print_if'],
    };
    const colours = ['off', 'white', 'warmwhite', 'red', 'orange', 'yellow', 'lime', 'green', 'teal', 'cyan', 'blue',
        'purple', 'magenta', 'pink'];
    const kind = {};
    for (const [k, words] of Object.entries(groups)) {
        for (const w of words) {
            kind[w] = k;
        }
    }

    const style = document.createElement('style');
    style.textContent = `
.hl-wrap{position:relative}
.hl-wrap .hl-pre,.hl-wrap .expression_input{font-family:var(--bs-font-monospace);font-size:.9rem;line-height:1.5;
 white-space:pre-wrap;overflow-wrap:anywhere;tab-size:4}
.hl-wrap .hl-pre{position:absolute;inset:0;margin:0;pointer-events:none;overflow:hidden;color:#d4d4d4;
 background:#1e1e1e;border:1px solid transparent;border-radius:.375rem;padding:.375rem .75rem}
.hl-wrap .expression_input{position:relative;background:transparent!important;color:transparent;caret-color:#fff;
 border-color:#3c3c3c;resize:none;overflow:hidden}
.hl-wrap .expression_input:focus{border-color:#007fd4;box-shadow:0 0 0 .2rem rgba(0,127,212,.3)}
.hl-wrap .expression_input::selection{background:rgba(38,79,120,.85);color:transparent}
.hl-wrap .expression_input.is-invalid{border-color:#f44747}
.hl-comment{color:#6a9955;font-style:italic}.hl-regnum{color:#ce9178;font-weight:600}.hl-num{color:#b5cea8}
.hl-hex{color:#4fc1ff}.hl-reg{color:#c586c0;font-weight:600}.hl-logic{color:#569cd6}.hl-math{color:#dcdcaa}
.hl-input{color:#4ec9b0}.hl-stack{color:#9cdcfe}.hl-dbg{color:#f44747}.hl-colour{color:#d7ba7d}
.hl-bad{color:#f44747;text-decoration:underline wavy #f44747}
`;
    document.head.appendChild(style);

    const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

    function render(text) {
        const tokens = text.match(/\/\*[\s\S]*?(?:\*\/|$)|\s+|[^\s]+/g) || [];
        let out = '';
        for (let i = 0; i < tokens.length; i++) {
            const t = tokens[i];
            let cls = '';
            if (t.startsWith('/*')) {
                cls = 'comment';
            } else if (/^\s/.test(t)) {
                out += esc(t);
                continue;
            } else {
                const w = t.toLowerCase();
                if (/^0x[0-9a-f]+$/.test(w)) {
                    cls = 'hex';
                } else if (/^-?[0-9.]+$/.test(w)) {
                    let j = i + 1;
                    while ((j < tokens.length) && (/^\s/.test(tokens[j]) || tokens[j].startsWith('/*'))) {
                        j++;
                    }
                    const next = (j < tokens.length) ? tokens[j].toLowerCase() : '';
                    cls = ((next === 'store') || (next === 'recall')) ? 'regnum' : 'num';
                } else if (w in kind) {
                    cls = kind[w];
                } else if (colours.includes(w.split(':')[0]) && /^[a-z]+(:[0-9a-f]{1,2})?$/.test(w)) {
                    cls = 'colour';
                } else {
                    cls = 'bad';
                }
            }
            out += '<span class="hl-' + cls + '">' + esc(t) + '</span>';
        }
        return out + '\n';
    }

    function attach(ta) {
        if (ta.dataset.hl) {
            return;
        }
        ta.dataset.hl = '1';
        const wrap = document.createElement('div');
        wrap.className = 'hl-wrap';
        ta.parentNode.insertBefore(wrap, ta);
        const pre = document.createElement('pre');
        pre.className = 'hl-pre';
        pre.setAttribute('aria-hidden', 'true');
        wrap.appendChild(pre);
        wrap.appendChild(ta);
        let last = null;
        const update = () => {
            if (ta.value !== last) {
                last = ta.value;
                pre.innerHTML = render(ta.value);
            }
            ta.style.height = 'auto';
            ta.style.height = ta.scrollHeight + 2 + 'px';
        };
        ta.addEventListener('input', update);
        window.addEventListener('resize', () => { last = null; update(); });
        update();
        // the app sets .value directly when loading a config, without an input event
        setInterval(update, 250);
    }

    const scan = () => document.querySelectorAll('.expression_input').forEach(attach);
    new MutationObserver(scan).observe(document.documentElement, { childList: true, subtree: true });
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', scan);
    } else {
        scan();
    }
})();
