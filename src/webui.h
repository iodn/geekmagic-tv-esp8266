#ifndef WEBUI_H
#define WEBUI_H

#include <Arduino.h>
#include <pgmspace.h>

const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartClock</title>
<script>
(() => {
    try {
        const savedTheme = localStorage.getItem("smartclock-ui-theme");
        const prefersDark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
        document.documentElement.dataset.uiTheme =
            savedTheme === "dark" || savedTheme === "light"
                ? savedTheme
                : (prefersDark ? "dark" : "light");
    } catch (error) {
        document.documentElement.dataset.uiTheme = "light";
    }
})();
</script>
<style>
:root {
    --bg: #eef2f9;
    --bg-top: #f8f9ff;
    --bg-glow: rgba(208, 226, 255, 0.95);
    --surface: #f8f9ff;
    --surface-dim: #eef2f8;
    --surface-high: rgba(245, 247, 252, 0.94);
    --surface-highest: #e7ebf3;
    --surface-tint: rgba(255, 255, 255, 0.72);
    --panel: rgba(248, 249, 255, 0.92);
    --panel-strong: #f2f5fb;
    --panel-layer: rgba(248, 249, 255, 0.92);
    --panel-border: rgba(108, 117, 135, 0.18);
    --chip-bg: #e8edf7;
    --line: #c4c9d4;
    --line-strong: #a4acba;
    --ink: #151b26;
    --muted: #566070;
    --accent: #0b57d0;
    --accent-strong: #0b57d0;
    --accent-soft: #d3e3fd;
    --accent-ring: rgba(11, 87, 208, 0.14);
    --accent-border: rgba(11, 87, 208, 0.18);
    --danger: #ba1a1a;
    --danger-soft: #ffdad6;
    --danger-border: rgba(186, 26, 26, 0.18);
    --text-on-accent: #ffffff;
    --shadow: 0 1px 2px rgba(18, 24, 33, 0.06), 0 18px 36px rgba(18, 24, 33, 0.08);
    --radius: 28px;
    --radius-m: 20px;
    --radius-s: 16px;
    --touch: 48px;
    --nav-width: 120px;
    --mono: "Roboto Mono", "IBM Plex Mono", "SFMono-Regular", "Menlo", monospace;
    --sans: "Roboto Flex", "Roboto", "Segoe UI Variable", "Noto Sans", sans-serif;
}
html {
    color-scheme: light;
}
html[data-ui-theme="dark"] {
    color-scheme: dark;
    --bg: #10131a;
    --bg-top: #181c25;
    --bg-glow: rgba(82, 148, 255, 0.12);
    --surface: #11131a;
    --surface-dim: #161922;
    --surface-high: rgba(31, 36, 47, 0.96);
    --surface-highest: #272d39;
    --surface-tint: rgba(22, 25, 34, 0.78);
    --panel: rgba(23, 27, 35, 0.94);
    --panel-strong: #20252f;
    --panel-layer: rgba(23, 27, 35, 0.94);
    --panel-border: rgba(195, 198, 208, 0.12);
    --chip-bg: #252b37;
    --line: #424956;
    --line-strong: #5b6474;
    --ink: #edf1fb;
    --muted: #adb6c7;
    --accent: #a8c7fa;
    --accent-strong: #d3e3fd;
    --accent-soft: #284777;
    --accent-ring: rgba(168, 199, 250, 0.18);
    --accent-border: rgba(168, 199, 250, 0.18);
    --danger: #ffb4ab;
    --danger-soft: #5c1d1d;
    --danger-border: rgba(255, 180, 171, 0.18);
    --shadow: 0 1px 2px rgba(0, 0, 0, 0.28), 0 20px 44px rgba(0, 0, 0, 0.36);
}
* {
    box-sizing: border-box;
}
html, body {
    margin: 0;
    min-height: 100%;
}
body {
    font-family: var(--sans);
    color: var(--ink);
    background:
        radial-gradient(circle at top left, var(--bg-glow), transparent 30%),
        radial-gradient(circle at bottom right, rgba(255, 255, 255, 0.22), transparent 24%),
        linear-gradient(180deg, var(--bg-top) 0%, var(--bg) 100%);
}
body.auth-open {
    overflow: hidden;
}
button,
select,
input,
textarea {
    font: inherit;
}
.app {
    max-width: 1480px;
    margin: 0 auto;
    padding: 24px 20px 36px;
}
.topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 18px;
    position: sticky;
    top: 10px;
    z-index: 12;
    margin-bottom: 22px;
    padding: 14px 18px;
    border-radius: 30px;
    border: 1px solid var(--panel-border);
    background: var(--surface-tint);
    box-shadow: var(--shadow);
    backdrop-filter: blur(18px);
}
.brand {
    display: flex;
    align-items: center;
    gap: 16px;
    min-width: 0;
}
.brand-mark {
    width: 44px;
    height: 44px;
    border-radius: 16px;
    display: grid;
    place-items: center;
    background: var(--accent-soft);
    color: var(--accent-strong);
    box-shadow: inset 0 0 0 1px var(--accent-border);
}
.brand-mark::before {
    content: "";
    width: 18px;
    height: 18px;
    border-radius: 8px;
    background: currentColor;
}
.brand h1 {
    margin: 0;
    font-size: 1.28rem;
    font-weight: 600;
    letter-spacing: -0.03em;
}
.brand small {
    display: block;
    color: var(--muted);
    font-size: 0.88rem;
    margin-top: 2px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.top-actions {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: 10px;
    flex-wrap: wrap;
}
.auth-overlay {
    position: fixed;
    inset: 0;
    z-index: 20;
    display: none;
    align-items: center;
    justify-content: center;
    padding: 20px;
    background: rgba(13, 16, 24, 0.46);
    backdrop-filter: blur(18px);
}
.auth-overlay.active {
    display: flex;
}
.auth-card {
    width: min(100%, 420px);
    padding: 24px;
    border-radius: 32px;
    border: 1px solid var(--panel-border);
    background: var(--surface);
    box-shadow: var(--shadow);
}
.auth-card h2 {
    margin: 0;
    font-size: 1.4rem;
    letter-spacing: -0.03em;
}
.auth-card p {
    margin: 12px 0 0;
    color: var(--muted);
    font-size: 0.92rem;
    line-height: 1.55;
}
.auth-device {
    display: grid;
    gap: 6px;
    margin-bottom: 18px;
    padding: 16px 18px;
    border-radius: 24px;
    border: 1px solid var(--panel-border);
    background: var(--panel-strong);
}
.auth-device-label {
    color: var(--muted);
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.08em;
    text-transform: uppercase;
}
.auth-device-name {
    color: var(--ink);
    font-size: 1.05rem;
    font-weight: 600;
    letter-spacing: -0.02em;
}
.auth-device-host {
    color: var(--muted);
    font-family: var(--mono);
    font-size: 0.78rem;
    word-break: break-all;
}
.auth-user {
    display: inline-flex;
    align-items: center;
    min-height: 40px;
    margin-top: 16px;
    padding: 0 14px;
    border-radius: 999px;
    border: 1px solid var(--panel-border);
    background: var(--chip-bg);
    color: var(--ink);
    font-family: var(--mono);
    font-size: 0.82rem;
}
.auth-form {
    display: grid;
    gap: 14px;
    margin-top: 20px;
}
.auth-message {
    min-height: 1.2em;
    color: var(--muted);
    font-size: 0.85rem;
}
.auth-message.error {
    color: var(--danger);
}
.auth-support {
    display: grid;
    gap: 12px;
    margin-top: 18px;
}
.auth-note {
    padding: 14px 16px;
    border-radius: 22px;
    background: var(--panel-strong);
    color: var(--muted);
    font-size: 0.82rem;
    line-height: 1.5;
}
.auth-note strong {
    display: block;
    margin-bottom: 4px;
    color: var(--ink);
    font-size: 0.84rem;
}
.auth-note code {
    font-family: var(--mono);
}
.pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 38px;
    padding: 0 14px;
    border-radius: 999px;
    border: 1px solid transparent;
    background: var(--chip-bg);
    color: var(--ink);
    font-size: 0.84rem;
    font-weight: 600;
}
.pill.live,
.pill.saved {
    background: var(--accent-soft);
    color: var(--accent-strong);
}
.pill.pending {
    background: var(--surface-highest);
    color: var(--ink);
}
.pill.error {
    color: var(--danger);
    background: var(--danger-soft);
}
.shell {
    display: grid;
    grid-template-columns: var(--nav-width) minmax(0, 1fr);
    gap: 22px;
    align-items: start;
}
.workspace {
    display: grid;
    gap: 20px;
    min-width: 0;
}
.tabs {
    display: grid;
    gap: 10px;
    position: sticky;
    top: 112px;
    padding: 12px 8px;
    border-radius: 32px;
    border: 1px solid var(--panel-border);
    background: var(--panel);
    box-shadow: var(--shadow);
}
.tab {
    min-height: 76px;
    padding: 12px 8px;
    border: none;
    border-radius: 24px;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
    font-weight: 600;
    display: grid;
    justify-items: center;
    align-content: center;
    gap: 8px;
    box-shadow: none;
}
.tab-icon {
    width: 36px;
    height: 36px;
    border-radius: 18px;
    display: grid;
    place-items: center;
    color: currentColor;
}
.tab-icon svg {
    width: 20px;
    height: 20px;
    stroke: currentColor;
    fill: none;
    stroke-width: 2;
    stroke-linecap: round;
    stroke-linejoin: round;
}
.tab-label {
    font-size: 0.76rem;
    letter-spacing: 0.01em;
    text-align: center;
    line-height: 1.15;
}
.tab.active {
    background: var(--chip-bg);
    color: var(--ink);
}
.tab.active .tab-icon {
    background: var(--accent-soft);
    color: var(--accent-strong);
}
.panel {
    background: var(--panel-layer);
    border: 1px solid var(--panel-border);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
    padding: 24px;
}
.display-section {
    display: grid;
    gap: 14px;
    padding: 18px;
    border-radius: 24px;
    border: 1px solid var(--panel-border);
    background: var(--panel-strong);
}
.display-section + .display-section {
    margin-top: 16px;
}
.display-section-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 12px;
}
.display-section-title {
    margin: 0;
    font-size: 0.98rem;
    font-weight: 600;
    letter-spacing: -0.02em;
}
.display-section-copy {
    margin: 6px 0 0;
    color: var(--muted);
    font-size: 0.84rem;
    line-height: 1.45;
}
.panel-stack,
.content-grid,
.system-grid {
    display: grid;
    gap: 20px;
}
.content-grid,
.system-grid {
    grid-template-columns: minmax(0, 1.08fr) minmax(300px, 0.92fr);
}
.panel-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 18px;
}
.panel-title {
    margin: 0;
    font-size: 1.08rem;
    font-weight: 600;
    letter-spacing: -0.03em;
}
.meta-inline {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
}
.meta-chip {
    padding: 7px 12px;
    border-radius: 999px;
    background: var(--chip-bg);
    border: 1px solid transparent;
    color: var(--muted);
    font-size: 0.76rem;
    font-family: var(--mono);
}
.grid {
    display: grid;
    gap: 16px;
}
.grid.two {
    grid-template-columns: repeat(2, minmax(0, 1fr));
}
.grid.three {
    grid-template-columns: repeat(3, minmax(0, 1fr));
}
.field {
    display: grid;
    gap: 8px;
}
.field label,
.section-label,
summary {
    font-size: 0.76rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--muted);
}
.field small,
.muted {
    color: var(--muted);
    font-size: 0.8rem;
    line-height: 1.45;
}
input[type="text"],
input[type="number"],
input[type="password"],
input[type="time"],
select,
textarea {
    width: 100%;
    min-height: 56px;
    padding: 16px;
    border-radius: 18px;
    border: 1px solid transparent;
    background: var(--panel-strong);
    color: var(--ink);
    transition: border-color 140ms ease, box-shadow 140ms ease, background 140ms ease;
}
input[type="text"]:focus,
input[type="number"]:focus,
input[type="password"]:focus,
input[type="time"]:focus,
select:focus,
textarea:focus {
    outline: none;
    border-color: var(--accent-border);
    box-shadow: 0 0 0 4px var(--accent-ring);
    background: var(--surface);
}
textarea {
    min-height: 120px;
    resize: vertical;
}
input[type="range"] {
    width: 100%;
    accent-color: var(--accent);
}
.slider-row {
    display: grid;
    grid-template-columns: 1fr auto;
    align-items: center;
    gap: 10px;
}
.value-badge {
    min-width: 54px;
    text-align: center;
    padding: 12px 14px;
    border-radius: 16px;
    border: 1px solid transparent;
    background: var(--panel-strong);
    font-family: var(--mono);
}
.theme-picker {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
}
.theme-button {
    min-height: 92px;
    padding: 14px 16px;
    border-radius: 20px;
    border: 1px solid transparent;
    background: var(--panel-strong);
    color: var(--ink);
    cursor: pointer;
    font-weight: 600;
    display: grid;
    align-content: start;
    justify-items: start;
    gap: 12px;
}
.theme-button.active {
    color: var(--accent-strong);
    background: var(--accent-soft);
}
.theme-preview {
    display: flex;
    gap: 8px;
}
.theme-swatch {
    width: 18px;
    height: 18px;
    border-radius: 9px;
    border: 1px solid rgba(0, 0, 0, 0.12);
}
.theme-button-title {
    font-size: 0.92rem;
    font-weight: 600;
}
.color-grid {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
}
.color-card {
    padding: 14px;
    border-radius: 22px;
    background: var(--panel-strong);
}
.color-row {
    display: grid;
    grid-template-columns: 56px 1fr;
    gap: 10px;
    align-items: center;
}
input[type="color"] {
    width: 56px;
    min-width: 56px;
    height: 56px;
    padding: 6px;
    border-radius: 18px;
    border: 1px solid transparent;
    background: var(--surface);
    cursor: pointer;
}
.toggle-grid {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(2, minmax(0, 1fr));
}
.toggle {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 64px;
    padding: 14px 16px;
    border-radius: 22px;
    border: 1px solid transparent;
    background: var(--panel-strong);
}
.toggle span {
    font-size: 0.92rem;
    font-weight: 600;
}
.toggle input[type="checkbox"] {
    appearance: none;
    -webkit-appearance: none;
    position: relative;
    width: 52px;
    min-width: 52px;
    height: 32px;
    border-radius: 999px;
    background: var(--line);
    cursor: pointer;
    transition: background 140ms ease;
}
.toggle input[type="checkbox"]::before {
    content: "";
    position: absolute;
    top: 4px;
    left: 4px;
    width: 24px;
    height: 24px;
    border-radius: 50%;
    background: var(--surface);
    box-shadow: 0 1px 2px rgba(0, 0, 0, 0.24);
    transition: transform 140ms ease, background 140ms ease;
}
.toggle input[type="checkbox"]:checked {
    background: var(--accent);
}
.toggle input[type="checkbox"]:checked::before {
    transform: translateX(20px);
    background: var(--text-on-accent);
}
.chip-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
}
.chip-toggle {
    position: relative;
}
.chip-toggle input {
    position: absolute;
    opacity: 0;
    pointer-events: none;
}
.chip-toggle span {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 44px;
    padding: 0 14px;
    border-radius: 999px;
    border: 1px solid transparent;
    background: var(--panel-strong);
    color: var(--muted);
    cursor: pointer;
    font-weight: 600;
}
.chip-toggle input:checked + span {
    color: var(--accent-strong);
    background: var(--accent-soft);
}
details {
    border: 1px solid transparent;
    border-radius: 22px;
    background: var(--panel-strong);
    overflow: hidden;
}
details + details {
    margin-top: 10px;
}
summary {
    list-style: none;
    padding: 16px 18px;
    cursor: pointer;
}
summary::-webkit-details-marker {
    display: none;
}
.details-body {
    padding: 0 18px 18px;
}
.market-row,
.world-row {
    display: grid;
    gap: 12px;
    grid-template-columns: auto 1fr 1fr 1fr 1fr 1fr;
    align-items: end;
    margin-top: 14px;
}
.feed-stack,
.search-results {
    display: grid;
    gap: 10px;
}
.slot-card {
    padding: 18px;
    border-radius: 24px;
    border: 1px solid transparent;
    background: var(--panel-strong);
}
.search-result {
    text-align: left;
    justify-content: flex-start;
    min-height: 0;
    padding: 14px 16px;
    border-radius: 18px;
    border: 1px solid transparent;
    background: var(--panel-strong);
    color: var(--ink);
}
.search-result small {
    display: block;
    margin-top: 4px;
    color: var(--muted);
    font-weight: 500;
}
.hidden {
    display: none !important;
}
.world-row {
    grid-template-columns: auto 1fr 1fr;
}
.world-row input[type="checkbox"] {
    appearance: none;
    -webkit-appearance: none;
    width: 20px;
    height: 20px;
    margin: 0 4px 18px 0;
    border-radius: 6px;
    border: 2px solid var(--line-strong);
    background: transparent;
    cursor: pointer;
}
.world-row input[type="checkbox"]:checked {
    border-color: var(--accent);
    background: var(--accent);
    box-shadow: inset 0 0 0 3px var(--surface);
}
.market-row:first-of-type,
.world-row:first-of-type {
    margin-top: 0;
}
.button-row {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
}
.button,
button,
input[type="submit"] {
    min-height: var(--touch);
    padding: 0 18px;
    border: 1px solid transparent;
    border-radius: 999px;
    background: var(--accent);
    color: var(--text-on-accent);
    cursor: pointer;
    font-weight: 600;
    letter-spacing: 0.01em;
}
.ghost,
.button.secondary {
    background: var(--panel-strong);
    color: var(--ink);
    border: 1px solid var(--panel-border);
}
.danger {
    background: var(--danger);
    color: var(--text-on-accent);
}
.mode-switch {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 4px;
    border-radius: 999px;
    border: 1px solid var(--panel-border);
    background: var(--panel-strong);
}
.mode-button {
    min-height: 36px;
    padding: 0 14px;
    border: 1px solid transparent;
    border-radius: 999px;
    background: transparent;
    color: var(--muted);
    box-shadow: none;
}
.mode-button.active {
    background: var(--accent-soft);
    color: var(--ink);
}
.mode-button:hover {
    background: var(--surface-highest);
}
.button-link {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: var(--touch);
    padding: 0 18px;
    border-radius: 999px;
    border: 1px solid var(--panel-border);
    background: var(--panel-strong);
    color: var(--ink);
    text-decoration: none;
    font-weight: 600;
}
button:disabled,
input[type="submit"]:disabled,
.button-link[aria-disabled="true"] {
    opacity: 0.5;
    cursor: not-allowed;
    box-shadow: none;
}
.panel-note {
    margin-top: 12px;
    color: var(--muted);
    font-size: 0.8rem;
    line-height: 1.45;
    padding: 12px 14px;
    border-radius: 18px;
    background: var(--panel-strong);
}
.identity-preview {
    margin-top: 0;
    display: grid;
    gap: 10px;
}
.identity-line {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
}
.identity-line span {
    color: var(--muted);
}
.identity-line strong,
.identity-line code {
    color: var(--ink);
    font-size: 0.86rem;
}
.identity-line code {
    font-family: var(--mono);
    text-align: right;
    word-break: break-all;
}
.status-list {
    display: grid;
    gap: 10px;
}
.status-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 52px;
    padding: 12px 14px;
    border-radius: 18px;
    border: 1px solid transparent;
    background: var(--panel-strong);
}
.status-item span:last-child {
    color: var(--muted);
    font-family: var(--mono);
    font-size: 0.8rem;
}
.tab-panel {
    display: none;
}
.tab-panel.active {
    display: grid;
    gap: 20px;
}
@media (max-width: 1120px) {
    .app {
        padding-bottom: 112px;
    }
    .topbar {
        position: static;
    }
    .shell {
        grid-template-columns: 1fr;
    }
    .tabs {
        position: fixed;
        left: 16px;
        right: 16px;
        bottom: 16px;
        top: auto;
        z-index: 15;
        grid-template-columns: repeat(6, minmax(0, 1fr));
        gap: 6px;
        padding: 10px;
        border-radius: 28px;
        backdrop-filter: blur(18px);
    }
    .tab {
        min-height: 68px;
        padding: 10px 6px;
        gap: 6px;
    }
    .tab-icon {
        width: 32px;
        height: 32px;
    }
    .tab-label {
        font-size: 0.68rem;
        line-height: 1.05;
    }
}
@media (max-width: 980px) {
    .content-grid,
    .system-grid,
    .grid.two,
    .grid.three,
    .color-grid,
    .toggle-grid,
    .market-row,
    .world-row {
        grid-template-columns: 1fr;
    }
    .topbar {
        flex-direction: column;
        align-items: flex-start;
    }
    .top-actions {
        width: 100%;
        justify-content: flex-start;
    }
}
@media (max-width: 720px) {
    .app {
        padding: 16px 14px 106px;
    }
    .topbar {
        margin-bottom: 16px;
        padding: 14px;
        border-radius: 24px;
    }
    .brand {
        width: 100%;
    }
    .brand small {
        max-width: 100%;
    }
    .panel {
        padding: 18px;
        border-radius: 24px;
    }
    .toggle-grid {
        grid-template-columns: 1fr;
    }
    .button-row > * {
        flex: 1 1 140px;
    }
}
</style>
</head>
<body>
<div class="app">
    <header class="topbar">
        <div class="brand">
            <div class="brand-mark"></div>
            <div>
                <h1>SmartClock</h1>
                <small id="deviceMeta">Loading…</small>
            </div>
        </div>
        <div class="top-actions">
            <div class="mode-switch" role="group" aria-label="Interface theme">
                <button class="mode-button" type="button" data-ui-theme-button="light" onclick="setUiTheme('light')">Light</button>
                <button class="mode-button" type="button" data-ui-theme-button="dark" onclick="setUiTheme('dark')">Dark</button>
            </div>
            <span id="syncState" class="pill">Ready</span>
            <button id="saveDisplayButton" type="button" onclick="commitState()">Save changes</button>
            <button id="discardDisplayButton" class="ghost" type="button" onclick="discardState()">Discard</button>
            <button class="ghost" type="button" onclick="loadAllState()">Refresh</button>
            <button id="signOutButton" class="ghost hidden" type="button" onclick="logoutSession()">Sign out</button>
        </div>
    </header>

    <div class="shell">
    <nav class="tabs" aria-label="Sections">
        <button class="tab active" data-tab="display" type="button" aria-pressed="true">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <path d="M5 7h14"></path>
                    <path d="M5 12h8"></path>
                    <path d="M5 17h14"></path>
                    <circle cx="17" cy="12" r="2"></circle>
                </svg>
            </span>
            <span class="tab-label">Display</span>
        </button>
        <button class="tab" data-tab="feeds" type="button" aria-pressed="false">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <path d="M6 17.5h10a4 4 0 0 0 .4-8A5 5 0 0 0 6.8 10"></path>
                    <path d="M8 19.5v-4"></path>
                    <path d="M12 19.5v-6"></path>
                    <path d="M16 19.5v-3"></path>
                </svg>
            </span>
            <span class="tab-label">Feeds</span>
        </button>
        <button class="tab" data-tab="homeassistant" type="button" aria-pressed="false">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <path d="M4 10.5 12 4l8 6.5"></path>
                    <path d="M6 9.5V20h12V9.5"></path>
                    <circle cx="12" cy="13" r="1.6"></circle>
                    <path d="M12 14.6v2.9"></path>
                </svg>
            </span>
            <span class="tab-label">Home Asst</span>
        </button>
        <button class="tab" data-tab="widgets" type="button" aria-pressed="false">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <rect x="4" y="4" width="7" height="7" rx="1.8"></rect>
                    <rect x="13" y="4" width="7" height="7" rx="1.8"></rect>
                    <rect x="4" y="13" width="7" height="7" rx="1.8"></rect>
                    <rect x="13" y="13" width="7" height="7" rx="1.8"></rect>
                </svg>
            </span>
            <span class="tab-label">Widgets</span>
        </button>
        <button class="tab" data-tab="network" type="button" aria-pressed="false">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <path d="M4 10.5a12 12 0 0 1 16 0"></path>
                    <path d="M7.5 14a7.5 7.5 0 0 1 9 0"></path>
                    <path d="M10.5 17.5a3.7 3.7 0 0 1 3 0"></path>
                    <circle cx="12" cy="20" r="1"></circle>
                </svg>
            </span>
            <span class="tab-label">Network</span>
        </button>
        <button class="tab" data-tab="system" type="button" aria-pressed="false">
            <span class="tab-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24">
                    <circle cx="12" cy="12" r="3.2"></circle>
                    <path d="M12 4.5v2.1"></path>
                    <path d="M12 17.4v2.1"></path>
                    <path d="M19.5 12h-2.1"></path>
                    <path d="M6.6 12H4.5"></path>
                    <path d="M17.3 6.7 15.8 8.2"></path>
                    <path d="M8.2 15.8 6.7 17.3"></path>
                    <path d="M17.3 17.3 15.8 15.8"></path>
                    <path d="M8.2 8.2 6.7 6.7"></path>
                </svg>
            </span>
            <span class="tab-label">System</span>
        </button>
    </nav>

    <main class="workspace">
    <section id="panel-display" class="tab-panel active">
        <div class="panel-stack">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Display</h2>
                    <div class="meta-inline">
                        <span class="meta-chip" id="firmwareMeta">--</span>
                        <span class="meta-chip" id="storageMeta">--</span>
                    </div>
                </div>

                <section class="display-section">
                    <div class="display-section-header">
                        <div>
                            <h3 class="display-section-title">Clock</h3>
                            <p class="display-section-copy">Timezone, time format, and page rotation.</p>
                        </div>
                    </div>

                    <div class="grid two">
                        <div class="field">
                            <label for="timezonePreset">Timezone</label>
                            <select data-live id="timezonePreset"></select>
                            <div id="timezoneCustomWrap" class="field hidden">
                                <input data-live type="number" id="timezoneCustom" placeholder="Offset in seconds">
                            </div>
                        </div>
                        <div class="field">
                            <label for="rotationInterval">Rotation interval</label>
                            <input data-live type="number" id="rotationInterval" min="3" max="120">
                        </div>
                    </div>

                    <div class="toggle-grid">
                        <label class="toggle"><span>24-hour</span><input data-live type="checkbox" id="use24Hour"></label>
                        <label class="toggle"><span>Seconds</span><input data-live type="checkbox" id="showSeconds"></label>
                        <label class="toggle"><span>Header IP</span><input data-live type="checkbox" id="showIp"></label>
                        <label class="toggle"><span>Auto rotate</span><input data-live type="checkbox" id="rotationEnabled"></label>
                    </div>
                </section>

                <section class="display-section">
                    <div class="display-section-header">
                        <div>
                            <h3 class="display-section-title">Visible on Device</h3>
                            <p class="display-section-copy">Choose which pages can rotate on screen.</p>
                        </div>
                    </div>

                    <div class="chip-grid">
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageClock"><span>Clock</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageWeather"><span>Weather</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageMarkets"><span>Markets</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageHome"><span>Home Assistant</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageFocus"><span>Focus</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageWorld"><span>World</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageEvent"><span>Event</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageQuote"><span>Quote</span></label>
                        <label class="chip-toggle"><input data-live data-page-toggle type="checkbox" id="pageStatus"><span>Status</span></label>
                    </div>
                </section>

                <section class="display-section">
                    <div class="display-section-header">
                        <div>
                            <h3 class="display-section-title">Appearance</h3>
                            <p class="display-section-copy">Daytime brightness and color theme.</p>
                        </div>
                    </div>

                    <div class="field">
                        <label for="brightness">Brightness</label>
                        <div class="slider-row">
                            <input data-live type="range" id="brightness" min="0" max="100" value="70">
                            <div class="value-badge" id="brightnessValue">70%</div>
                        </div>
                    </div>

                    <div class="field">
                        <label>Theme</label>
                        <input type="hidden" id="themePreset" value="0">
                        <div class="theme-picker" id="themePicker"></div>
                    </div>

                    <div class="toggle-grid">
                        <label class="toggle"><span>Custom colors</span><input data-live type="checkbox" id="customThemeEnabled"></label>
                    </div>

                    <div id="customThemeFields" class="color-grid hidden">
                        <div class="field color-card">
                            <label for="customAccentHex">Accent</label>
                            <div class="color-row">
                                <input data-live type="color" id="customAccentPicker">
                                <input data-live type="text" id="customAccentHex" placeholder="#58DAC1" maxlength="7" spellcheck="false">
                            </div>
                        </div>
                        <div class="field color-card">
                            <label for="customBackgroundHex">Background</label>
                            <div class="color-row">
                                <input data-live type="color" id="customBackgroundPicker">
                                <input data-live type="text" id="customBackgroundHex" placeholder="#080F1D" maxlength="7" spellcheck="false">
                            </div>
                        </div>
                        <div class="field color-card">
                            <label for="customSurfaceHex">Panel</label>
                            <div class="color-row">
                                <input data-live type="color" id="customSurfacePicker">
                                <input data-live type="text" id="customSurfaceHex" placeholder="#122035" maxlength="7" spellcheck="false">
                            </div>
                        </div>
                        <div class="field color-card">
                            <label for="customTextHex">Text</label>
                            <div class="color-row">
                                <input data-live type="color" id="customTextPicker">
                                <input data-live type="text" id="customTextHex" placeholder="#F0F7FF" maxlength="7" spellcheck="false">
                            </div>
                        </div>
                    </div>
                </section>

                <section class="display-section">
                    <div class="display-section-header">
                        <div>
                            <h3 class="display-section-title">Night Mode</h3>
                            <p class="display-section-copy">Optional schedule for lower brightness or a different palette.</p>
                        </div>
                    </div>

                    <div class="toggle-grid">
                        <label class="toggle"><span>Enable schedule</span><input data-live type="checkbox" id="nightModeEnabled"></label>
                    </div>

                    <div id="nightModeFields" class="panel-stack">
                        <div class="toggle-grid">
                            <label class="toggle"><span>Override theme</span><input data-live type="checkbox" id="nightThemeEnabled"></label>
                        </div>

                        <div class="grid two">
                            <div class="field">
                                <label for="nightStart">Start</label>
                                <input data-live type="time" id="nightStart" step="60">
                            </div>
                            <div class="field">
                                <label for="nightEnd">End</label>
                                <input data-live type="time" id="nightEnd" step="60">
                            </div>
                        </div>

                        <div class="field">
                            <label for="nightBrightness">Night brightness</label>
                            <div class="slider-row">
                                <input data-live type="range" id="nightBrightness" min="1" max="100" value="14">
                                <div class="value-badge" id="nightBrightnessValue">14%</div>
                            </div>
                        </div>

                        <div id="nightThemePresetWrap" class="field">
                            <label>Night theme</label>
                            <input type="hidden" id="nightThemePreset" value="0">
                            <div class="theme-picker" id="nightThemePicker"></div>
                        </div>

                        <div id="nightCustomThemeToggle" class="toggle-grid">
                            <label class="toggle"><span>Night custom colors</span><input data-live type="checkbox" id="nightCustomThemeEnabled"></label>
                        </div>

                        <div id="nightCustomThemeFields" class="color-grid hidden">
                            <div class="field color-card">
                                <label for="nightCustomAccentHex">Accent</label>
                                <div class="color-row">
                                    <input data-live type="color" id="nightCustomAccentPicker">
                                    <input data-live type="text" id="nightCustomAccentHex" placeholder="#A8C7FA" maxlength="7" spellcheck="false">
                                </div>
                            </div>
                            <div class="field color-card">
                                <label for="nightCustomBackgroundHex">Background</label>
                                <div class="color-row">
                                    <input data-live type="color" id="nightCustomBackgroundPicker">
                                    <input data-live type="text" id="nightCustomBackgroundHex" placeholder="#050B17" maxlength="7" spellcheck="false">
                                </div>
                            </div>
                            <div class="field color-card">
                                <label for="nightCustomSurfaceHex">Panel</label>
                                <div class="color-row">
                                    <input data-live type="color" id="nightCustomSurfacePicker">
                                    <input data-live type="text" id="nightCustomSurfaceHex" placeholder="#101A28" maxlength="7" spellcheck="false">
                                </div>
                            </div>
                            <div class="field color-card">
                                <label for="nightCustomTextHex">Text</label>
                                <div class="color-row">
                                    <input data-live type="color" id="nightCustomTextPicker">
                                    <input data-live type="text" id="nightCustomTextHex" placeholder="#F3F6FC" maxlength="7" spellcheck="false">
                                </div>
                            </div>
                        </div>

                        <div class="panel-note">Night mode follows the device timezone and uses its own brightness percentage.</div>
                    </div>
                </section>
            </section>
        </div>
    </section>

    <section id="panel-feeds" class="tab-panel">
        <div class="content-grid">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Weather</h2>
                    <div class="meta-inline">
                        <span class="meta-chip" id="weatherFeedBadge">--</span>
                    </div>
                </div>

                <div class="grid two">
                    <div class="field">
                        <label for="weatherSource">Source</label>
                        <select data-feed-live id="weatherSource">
                            <option value="disabled">Disabled</option>
                            <option value="open-meteo">Open-Meteo</option>
                        </select>
                    </div>
                    <div class="field">
                        <label for="weatherRefresh">Refresh</label>
                        <input data-feed-live type="number" id="weatherRefresh" min="5" max="240">
                    </div>
                </div>

                <div class="toggle-grid" style="margin-top: 14px;">
                    <label class="toggle"><span>Fahrenheit</span><input data-feed-live type="checkbox" id="weatherUseFahrenheit"></label>
                </div>

                <div id="weatherAutoFields" class="feed-stack" style="margin-top: 14px;">
                    <div class="grid two">
                        <div class="field"><label for="weatherQuery">Search</label><input data-feed-live type="text" id="weatherQuery" placeholder="Tokyo"></div>
                        <div class="field"><label for="weatherLabel">Label</label><input data-feed-live type="text" id="weatherLabel" placeholder="Tokyo, JP"></div>
                    </div>
                    <div class="button-row">
                        <button type="button" class="ghost" onclick="searchWeatherLocations()">Find place</button>
                        <button type="button" class="ghost" onclick="syncFeeds('weather')">Sync weather</button>
                    </div>
                    <div class="grid two">
                        <div class="field"><label for="weatherLatitude">Latitude</label><input data-feed-live type="number" step="0.0001" id="weatherLatitude"></div>
                        <div class="field"><label for="weatherLongitude">Longitude</label><input data-feed-live type="number" step="0.0001" id="weatherLongitude"></div>
                    </div>
                    <div class="panel-note" id="weatherSearchStatus">Search a city or set coordinates directly.</div>
                    <div id="weatherResults" class="search-results"></div>
                </div>

            </section>

            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Providers</h2>
                </div>

                <div class="field">
                    <label for="finnhubApiKey">Finnhub API key</label>
                    <input data-feed-live type="password" id="finnhubApiKey" placeholder="Free key for stocks">
                    <small>Needed for Finnhub stock slots. Open-Meteo weather and CoinGecko crypto are keyless.</small>
                </div>

                <div class="status-list" style="margin-top: 14px;">
                    <div class="status-item"><span>Weather</span><span id="weatherFeedStatus">--</span></div>
                    <div class="status-item"><span>Market 1</span><span id="marketFeedStatus0">--</span></div>
                    <div class="status-item"><span>Market 2</span><span id="marketFeedStatus1">--</span></div>
                    <div class="status-item"><span>Market 3</span><span id="marketFeedStatus2">--</span></div>
                </div>

                <div class="button-row" style="margin-top: 14px;">
                    <button type="button" onclick="syncFeeds('all')">Sync all</button>
                    <button id="saveFeedsButton" type="button" class="ghost" onclick="commitFeedConfig()">Save feeds</button>
                    <button id="discardFeedsButton" type="button" class="ghost" onclick="discardFeedConfig()">Discard</button>
                    <button type="button" class="ghost danger" onclick="resetFeedDefaults()">Reset feeds</button>
                </div>
            </section>
        </div>

        <div class="panel-stack" style="margin-top: 16px;">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Markets</h2>
                </div>

                <div class="panel-stack">
                    <div class="slot-card">
                        <div class="panel-header">
                            <h3 class="panel-title">Slot 1</h3>
                            <span class="meta-chip" id="marketSlotMeta0">--</span>
                        </div>
                        <div class="grid three">
                            <div class="field">
                                <label for="marketSource0">Source</label>
                                <select data-feed-live id="marketSource0">
                                    <option value="disabled">Disabled</option>
                                    <option value="finnhub">Finnhub</option>
                                    <option value="coingecko">CoinGecko</option>
                                </select>
                            </div>
                            <div class="field">
                                <label for="feedMarketSymbol0">Symbol / ID</label>
                                <input data-feed-live type="text" id="feedMarketSymbol0" placeholder="AAPL or bitcoin">
                            </div>
                            <div class="field">
                                <label for="feedMarketLabel0">Label</label>
                                <input data-feed-live type="text" id="feedMarketLabel0" placeholder="Optional">
                            </div>
                        </div>
                        <div class="grid two" style="margin-top: 12px;">
                            <div class="field">
                                <label for="feedMarketCurrency0">Currency</label>
                                <input data-feed-live type="text" id="feedMarketCurrency0" placeholder="usd">
                            </div>
                            <div class="field">
                                <label for="feedMarketRefresh0">Refresh</label>
                                <input data-feed-live type="number" id="feedMarketRefresh0" min="1" max="240">
                            </div>
                        </div>
                        <div class="panel-note" id="marketHint0" style="margin-top: 12px;">CoinGecko accepts IDs, names, or symbols. Sync resolves the best match.</div>
                    </div>

                    <div class="slot-card">
                        <div class="panel-header">
                            <h3 class="panel-title">Slot 2</h3>
                            <span class="meta-chip" id="marketSlotMeta1">--</span>
                        </div>
                        <div class="grid three">
                            <div class="field">
                                <label for="marketSource1">Source</label>
                                <select data-feed-live id="marketSource1">
                                    <option value="disabled">Disabled</option>
                                    <option value="finnhub">Finnhub</option>
                                    <option value="coingecko">CoinGecko</option>
                                </select>
                            </div>
                            <div class="field">
                                <label for="feedMarketSymbol1">Symbol / ID</label>
                                <input data-feed-live type="text" id="feedMarketSymbol1" placeholder="MSFT or ethereum">
                            </div>
                            <div class="field">
                                <label for="feedMarketLabel1">Label</label>
                                <input data-feed-live type="text" id="feedMarketLabel1" placeholder="Optional">
                            </div>
                        </div>
                        <div class="grid two" style="margin-top: 12px;">
                            <div class="field">
                                <label for="feedMarketCurrency1">Currency</label>
                                <input data-feed-live type="text" id="feedMarketCurrency1" placeholder="usd">
                            </div>
                            <div class="field">
                                <label for="feedMarketRefresh1">Refresh</label>
                                <input data-feed-live type="number" id="feedMarketRefresh1" min="1" max="240">
                            </div>
                        </div>
                        <div class="panel-note" id="marketHint1" style="margin-top: 12px;">Finnhub needs an API key. CoinGecko can resolve crypto IDs, names, or symbols.</div>
                    </div>

                    <div class="slot-card">
                        <div class="panel-header">
                            <h3 class="panel-title">Slot 3</h3>
                            <span class="meta-chip" id="marketSlotMeta2">--</span>
                        </div>
                        <div class="grid three">
                            <div class="field">
                                <label for="marketSource2">Source</label>
                                <select data-feed-live id="marketSource2">
                                    <option value="disabled">Disabled</option>
                                    <option value="finnhub">Finnhub</option>
                                    <option value="coingecko">CoinGecko</option>
                                </select>
                            </div>
                            <div class="field">
                                <label for="feedMarketSymbol2">Symbol / ID</label>
                                <input data-feed-live type="text" id="feedMarketSymbol2" placeholder="NVDA or solana">
                            </div>
                            <div class="field">
                                <label for="feedMarketLabel2">Label</label>
                                <input data-feed-live type="text" id="feedMarketLabel2" placeholder="Optional">
                            </div>
                        </div>
                        <div class="grid two" style="margin-top: 12px;">
                            <div class="field">
                                <label for="feedMarketCurrency2">Currency</label>
                                <input data-feed-live type="text" id="feedMarketCurrency2" placeholder="usd">
                            </div>
                            <div class="field">
                                <label for="feedMarketRefresh2">Refresh</label>
                                <input data-feed-live type="number" id="feedMarketRefresh2" min="1" max="240">
                            </div>
                        </div>
                        <div class="panel-note" id="marketHint2" style="margin-top: 12px;">Use a live source or disable the slot.</div>
                    </div>
                </div>
            </section>

        </div>
    </section>

    <section id="panel-homeassistant" class="tab-panel">
        <div class="panel-stack">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Home Assistant</h2>
                    <div class="meta-inline">
                        <span class="meta-chip" id="homeAssistantFeedBadge">--</span>
                        <span class="meta-chip" id="homeAssistantFeedStatus">--</span>
                    </div>
                </div>

                <div class="toggle-grid">
                    <label class="toggle"><span>Enable feed</span><input data-feed-live type="checkbox" id="homeAssistantEnabled"></label>
                </div>

                <div id="homeAssistantFields" class="panel-stack" style="margin-top: 14px;">
                    <div class="grid two">
                        <div class="field">
                            <label for="homeAssistantBaseUrl">Base URL</label>
                            <input data-feed-live type="text" id="homeAssistantBaseUrl" placeholder="https://ha.example.com">
                        </div>
                        <div class="field">
                            <label for="homeAssistantRefresh">Refresh</label>
                            <input data-feed-live type="number" id="homeAssistantRefresh" min="1" max="240">
                        </div>
                    </div>

                    <div class="grid two">
                        <div class="field">
                            <label for="homeAssistantTlsMode">HTTPS validation</label>
                            <select data-feed-live id="homeAssistantTlsMode">
                                <option value="insecure">Allow self-signed / insecure</option>
                                <option value="fingerprint">Pinned fingerprint</option>
                            </select>
                        </div>
                        <div class="field" id="homeAssistantFingerprintField">
                            <label for="homeAssistantFingerprint">Fingerprint</label>
                            <input data-feed-live type="text" id="homeAssistantFingerprint" placeholder="40 hex chars">
                        </div>
                    </div>

                    <div class="field">
                        <label for="homeAssistantToken">Access token</label>
                        <input data-feed-live type="password" id="homeAssistantToken" placeholder="Long-lived access token">
                        <small>Use a dedicated Home Assistant long-lived token. Base URL accepts domains or IP addresses.</small>
                    </div>

                    <div class="panel-stack">
                        <div class="slot-card">
                            <div class="panel-header">
                                <h3 class="panel-title">Slot 1</h3>
                                <span class="meta-chip" id="haSlotMeta0">--</span>
                            </div>
                            <div class="grid three">
                                <label class="toggle"><span>Show</span><input data-feed-live type="checkbox" id="haEnabled0"></label>
                                <div class="field">
                                    <label for="haEntityId0">Entity ID</label>
                                    <input data-feed-live type="text" id="haEntityId0" placeholder="sensor.living_room_temperature">
                                </div>
                                <div class="field">
                                    <label for="haLabel0">Label</label>
                                    <input data-feed-live type="text" id="haLabel0" placeholder="Optional">
                                </div>
                            </div>
                            <div class="field" style="margin-top: 12px;">
                                <label for="haUnit0">Unit override</label>
                                <input data-feed-live type="text" id="haUnit0" placeholder="Optional">
                            </div>
                        </div>

                        <div class="slot-card">
                            <div class="panel-header">
                                <h3 class="panel-title">Slot 2</h3>
                                <span class="meta-chip" id="haSlotMeta1">--</span>
                            </div>
                            <div class="grid three">
                                <label class="toggle"><span>Show</span><input data-feed-live type="checkbox" id="haEnabled1"></label>
                                <div class="field">
                                    <label for="haEntityId1">Entity ID</label>
                                    <input data-feed-live type="text" id="haEntityId1" placeholder="sensor.office_power">
                                </div>
                                <div class="field">
                                    <label for="haLabel1">Label</label>
                                    <input data-feed-live type="text" id="haLabel1" placeholder="Optional">
                                </div>
                            </div>
                            <div class="field" style="margin-top: 12px;">
                                <label for="haUnit1">Unit override</label>
                                <input data-feed-live type="text" id="haUnit1" placeholder="Optional">
                            </div>
                        </div>

                        <div class="slot-card">
                            <div class="panel-header">
                                <h3 class="panel-title">Slot 3</h3>
                                <span class="meta-chip" id="haSlotMeta2">--</span>
                            </div>
                            <div class="grid three">
                                <label class="toggle"><span>Show</span><input data-feed-live type="checkbox" id="haEnabled2"></label>
                                <div class="field">
                                    <label for="haEntityId2">Entity ID</label>
                                    <input data-feed-live type="text" id="haEntityId2" placeholder="sensor.daily_energy">
                                </div>
                                <div class="field">
                                    <label for="haLabel2">Label</label>
                                    <input data-feed-live type="text" id="haLabel2" placeholder="Optional">
                                </div>
                            </div>
                            <div class="field" style="margin-top: 12px;">
                                <label for="haUnit2">Unit override</label>
                                <input data-feed-live type="text" id="haUnit2" placeholder="Optional">
                            </div>
                        </div>

                        <div class="slot-card">
                            <div class="panel-header">
                                <h3 class="panel-title">Slot 4</h3>
                                <span class="meta-chip" id="haSlotMeta3">--</span>
                            </div>
                            <div class="grid three">
                                <label class="toggle"><span>Show</span><input data-feed-live type="checkbox" id="haEnabled3"></label>
                                <div class="field">
                                    <label for="haEntityId3">Entity ID</label>
                                    <input data-feed-live type="text" id="haEntityId3" placeholder="alarm_control_panel.home">
                                </div>
                                <div class="field">
                                    <label for="haLabel3">Label</label>
                                    <input data-feed-live type="text" id="haLabel3" placeholder="Optional">
                                </div>
                            </div>
                            <div class="field" style="margin-top: 12px;">
                                <label for="haUnit3">Unit override</label>
                                <input data-feed-live type="text" id="haUnit3" placeholder="Optional">
                            </div>
                        </div>
                    </div>

                </div>

                <div class="button-row" style="margin-top: 16px;">
                    <button id="homeAssistantSyncButton" type="button" onclick="syncFeeds('home')">Sync Home Assistant</button>
                    <button id="saveHomeAssistantButton" type="button" class="ghost" onclick="commitFeedConfig()">Save feeds</button>
                    <button id="discardHomeAssistantButton" type="button" class="ghost" onclick="discardFeedConfig()">Discard</button>
                </div>
            </section>
        </div>
    </section>

    <section id="panel-widgets" class="tab-panel">
        <div class="button-row" style="margin-bottom: 16px;">
            <button type="button" class="ghost danger" onclick="resetDashboardDefaults()">Reset widgets</button>
        </div>
        <div class="content-grid">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Focus</h2>
                </div>

                <div class="field"><label for="focusLabel">Label</label><input data-live type="text" id="focusLabel" placeholder="Focus Session"></div>
                <div class="grid two" style="margin-top: 12px;">
                    <div class="field"><label for="focusDuration">Duration</label><input data-live type="number" id="focusDuration" min="1" max="240"></div>
                    <div class="field"><label for="focusRemaining">Remaining</label><input data-live type="number" id="focusRemaining" min="0" max="1440"></div>
                </div>
                <div class="toggle-grid" style="margin-top: 12px;">
                    <label class="toggle"><span>Running</span><input data-live type="checkbox" id="focusRunning"></label>
                    <label class="toggle"><span>Break</span><input data-live type="checkbox" id="focusBreakMode"></label>
                </div>

                <details open style="margin-top: 14px;">
                    <summary>World</summary>
                    <div class="details-body">
                        <div class="world-row">
                            <input data-live type="checkbox" id="worldEnabled0">
                            <div class="field"><label for="worldLabel0">Label</label><input data-live type="text" id="worldLabel0"></div>
                            <div class="field"><label for="worldOffset0">Offset</label><select data-live id="worldOffset0"></select></div>
                        </div>
                        <div class="world-row">
                            <input data-live type="checkbox" id="worldEnabled1">
                            <div class="field"><label for="worldLabel1">Label</label><input data-live type="text" id="worldLabel1"></div>
                            <div class="field"><label for="worldOffset1">Offset</label><select data-live id="worldOffset1"></select></div>
                        </div>
                    </div>
                </details>
            </section>

            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Text</h2>
                </div>

                <details open>
                    <summary>Event</summary>
                    <div class="details-body">
                        <div class="field"><label for="eventTitle">Title</label><input data-live type="text" id="eventTitle"></div>
                        <div class="field" style="margin-top: 12px;"><label for="eventSubtitle">Subtitle</label><input data-live type="text" id="eventSubtitle"></div>
                        <div class="field" style="margin-top: 12px;"><label for="eventCountdown">Countdown (minutes)</label><input data-live type="number" id="eventCountdown" min="0" max="10080"></div>
                    </div>
                </details>

                <details>
                    <summary>Quote</summary>
                    <div class="details-body">
                        <div class="field"><label for="quoteText">Text</label><textarea data-live id="quoteText"></textarea></div>
                        <div class="field" style="margin-top: 12px;"><label for="quoteAuthor">Author</label><input data-live type="text" id="quoteAuthor"></div>
                    </div>
                </details>

                <details>
                    <summary>Status</summary>
                    <div class="details-body">
                        <div class="field"><label for="statusLine1">Line 1</label><input data-live type="text" id="statusLine1"></div>
                        <div class="field" style="margin-top: 12px;"><label for="statusLine2">Line 2</label><input data-live type="text" id="statusLine2"></div>
                    </div>
                </details>
            </section>
        </div>
    </section>

    <section id="panel-network" class="tab-panel">
        <div class="panel">
            <div class="panel-header">
                <h2 class="panel-title">Network</h2>
            </div>
            <div class="grid two" style="margin-bottom: 14px;">
                <div class="field">
                    <label for="deviceName">Device name</label>
                    <input data-live type="text" id="deviceName" maxlength="31" placeholder="Desk clock">
                    <small>Shown on the login page and used for <code>.local</code> discovery after the next reconnect or reboot.</small>
                </div>
                <div class="panel-note identity-preview">
                    <div class="identity-line">
                        <span>Login page</span>
                        <strong id="deviceNamePreview">Loading…</strong>
                    </div>
                    <div class="identity-line">
                        <span>.local</span>
                        <code id="deviceHostPreview">Loading…</code>
                    </div>
                </div>
            </div>
            <div class="button-row">
                <button type="button" class="ghost" onclick="scanWiFi()">Scan</button>
                <button type="button" class="ghost" onclick="reconfigureWiFi()">Portal</button>
            </div>
            <div class="panel-note" id="scanStatus">Idle</div>
            <div class="grid two" style="margin-top: 14px;">
                <div class="field">
                    <label for="wifiNetwork">Network</label>
                    <select id="wifiNetwork"></select>
                </div>
                <div class="field">
                    <label for="wifiPassword">Password</label>
                    <input type="password" id="wifiPassword">
                </div>
            </div>
            <div class="button-row" style="margin-top: 14px;">
                <button type="button" onclick="connectWiFi()">Connect</button>
            </div>
        </div>
    </section>

    <section id="panel-system" class="tab-panel">
        <div class="system-grid">
            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">Image</h2>
                </div>
                <div class="field">
                    <label for="fileUpload">JPEG</label>
                    <input type="file" accept="image/jpeg" id="fileUpload">
                </div>
                <div class="button-row" style="margin-top: 14px;">
                    <button type="button" onclick="uploadImage()">Upload</button>
                </div>
                <div class="panel-note" id="uploadStatus">Choose a JPEG to upload.</div>
                <div class="field" style="margin-top: 14px;">
                    <label for="imagePath">Path</label>
                    <input type="text" id="imagePath" placeholder="/image/poster.jpg">
                </div>
                <div class="button-row" style="margin-top: 14px;">
                    <button type="button" class="ghost" onclick="displayImage()">Show</button>
                    <button type="button" class="ghost" onclick="previewTestCard()">Dashboard</button>
                </div>
            </section>

            <section class="panel">
                <div class="panel-header">
                    <h2 class="panel-title">System</h2>
                </div>
                <div class="status-list">
                    <div class="status-item"><span>Firmware</span><span id="systemVersion">--</span></div>
                    <div class="status-item"><span>Storage</span><span id="systemStorage">--</span></div>
                </div>
                <div class="button-row" style="margin-top: 14px;">
                    <a class="button-link" href="/update">Update</a>
                    <a class="button-link" href="/log">Logs</a>
                    <a class="button-link" href="/space.json">Space</a>
                </div>
                <div class="field" style="margin-top: 18px;">
                    <label for="currentAdminPassword">Admin password</label>
                    <small>Protects the dashboard and OTA access.</small>
                </div>
                <div class="grid two" style="margin-top: 12px;">
                    <div class="field">
                        <label for="currentAdminPassword">Current</label>
                        <input type="password" id="currentAdminPassword" autocomplete="current-password">
                    </div>
                    <div class="field">
                        <label for="newAdminPassword">New</label>
                        <input type="password" id="newAdminPassword" autocomplete="new-password">
                    </div>
                </div>
                <div class="field" style="margin-top: 12px;">
                    <label for="confirmAdminPassword">Confirm</label>
                    <input type="password" id="confirmAdminPassword" autocomplete="new-password">
                </div>
                <div class="button-row" style="margin-top: 14px;">
                    <button type="button" class="ghost" onclick="changeAdminPassword()">Update password</button>
                </div>
                <div class="panel-note" id="securityStatus">Signed in as <code>admin</code>.</div>
                <div class="button-row" style="margin-top: 14px;">
                    <button type="button" class="danger" onclick="factoryReset()">Factory Reset</button>
                </div>
            </section>
        </div>
    </section>
    </main>
    </div>
</div>

<div id="authOverlay" class="auth-overlay active" role="dialog" aria-modal="true" aria-labelledby="authTitle">
    <div class="auth-card">
        <div class="auth-device">
            <div class="auth-device-label">Device</div>
            <div class="auth-device-name" id="authDeviceName">Loading…</div>
            <div class="auth-device-host" id="authDeviceHost">Loading…</div>
        </div>
        <h2 id="authTitle">Admin login</h2>
        <p id="authHint">Sign in to manage the device.</p>
        <div class="auth-user">User: <span id="authUsernameLabel" style="margin-left: 6px;">admin</span></div>
        <form id="authForm" class="auth-form">
            <div class="field">
                <label for="authPassword">Password</label>
                <input type="password" id="authPassword" autocomplete="current-password" autofocus>
            </div>
            <div class="button-row">
                <button type="submit">Sign in</button>
                <button id="authRevealButton" type="button" class="ghost hidden" onclick="revealPasswordOnDevice()">Show password on device</button>
            </div>
            <div id="authMessage" class="auth-message"></div>
        </form>
        <div class="auth-support">
            <div class="auth-note" id="authSetupNote"></div>
            <div class="auth-note" id="authRecoveryNote"></div>
        </div>
    </div>
</div>

<script>
const timezoneOptions = [
    { value: -43200, label: "UTC-12:00" },
    { value: -39600, label: "UTC-11:00" },
    { value: -36000, label: "UTC-10:00" },
    { value: -34200, label: "UTC-09:30" },
    { value: -32400, label: "UTC-09:00" },
    { value: -28800, label: "UTC-08:00" },
    { value: -25200, label: "UTC-07:00" },
    { value: -21600, label: "UTC-06:00" },
    { value: -18000, label: "UTC-05:00" },
    { value: -14400, label: "UTC-04:00" },
    { value: -12600, label: "UTC-03:30" },
    { value: -10800, label: "UTC-03:00" },
    { value: -7200, label: "UTC-02:00" },
    { value: -3600, label: "UTC-01:00" },
    { value: 0, label: "UTC+00:00" },
    { value: 3600, label: "UTC+01:00" },
    { value: 7200, label: "UTC+02:00" },
    { value: 10800, label: "UTC+03:00" },
    { value: 12600, label: "UTC+03:30" },
    { value: 14400, label: "UTC+04:00" },
    { value: 16200, label: "UTC+04:30" },
    { value: 18000, label: "UTC+05:00" },
    { value: 19800, label: "UTC+05:30" },
    { value: 20700, label: "UTC+05:45" },
    { value: 21600, label: "UTC+06:00" },
    { value: 23400, label: "UTC+06:30" },
    { value: 25200, label: "UTC+07:00" },
    { value: 28800, label: "UTC+08:00" },
    { value: 31500, label: "UTC+08:45" },
    { value: 32400, label: "UTC+09:00" },
    { value: 34200, label: "UTC+09:30" },
    { value: 36000, label: "UTC+10:00" },
    { value: 37800, label: "UTC+10:30" },
    { value: 39600, label: "UTC+11:00" },
    { value: 43200, label: "UTC+12:00" },
    { value: 45900, label: "UTC+12:45" },
    { value: 46800, label: "UTC+13:00" },
    { value: 50400, label: "UTC+14:00" }
];

const themeLabels = [
    { value: 0, label: "Aurora", tones: ["#080F1D", "#122035", "#58DAC1"] },
    { value: 1, label: "Sunset", tones: ["#200C16", "#3A1727", "#FFA456"] },
    { value: 2, label: "Terminal", tones: ["#060E08", "#0C1D10", "#69FF6F"] }
];

const defaultCustomTheme = {
    background: "#080F1D",
    surface: "#122035",
    accent: "#58DAC1",
    text: "#F0F7FF"
};

const defaultNightCustomTheme = {
    background: "#050B17",
    surface: "#101A28",
    accent: "#A8C7FA",
    text: "#F3F6FC"
};

const dashboardPageControls = [
    { key: "clock", id: "pageClock" },
    { key: "weather", id: "pageWeather" },
    { key: "markets", id: "pageMarkets" },
    { key: "home", id: "pageHome" },
    { key: "focus", id: "pageFocus" },
    { key: "world", id: "pageWorld" },
    { key: "event", id: "pageEvent" },
    { key: "quote", id: "pageQuote" },
    { key: "status", id: "pageStatus" }
];

let hydrating = false;
let dirty = false;
let editGeneration = 0;
let feedEditGeneration = 0;
let widgetBaseline = {};
let operationQueue = Promise.resolve();

function queueOperation(operation) {
    const pending = operationQueue.then(operation);
    operationQueue = pending.catch(() => {});
    return pending;
}
let liveTimer = 0;
let livePromise = Promise.resolve();
let feedDirty = false;
let feedLiveTimer = 0;
let feedPromise = Promise.resolve();
let authenticated = false;
let authProvisionedThisBoot = false;
let authCanRevealPassword = false;
const uiThemeStorageKey = "smartclock-ui-theme";
const deviceIdentity = {
    deviceName: "SmartClock",
    defaultDeviceName: "SmartClock",
    hostName: "smartclock"
};

function currentUiTheme() {
    return document.documentElement.dataset.uiTheme === "dark" ? "dark" : "light";
}

function refreshUiThemeButtons() {
    const activeTheme = currentUiTheme();
    document.querySelectorAll("[data-ui-theme-button]").forEach((button) => {
        const isActive = button.dataset.uiThemeButton === activeTheme;
        button.classList.toggle("active", isActive);
        button.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
}

function setUiTheme(theme) {
    const nextTheme = theme === "dark" ? "dark" : "light";
    document.documentElement.dataset.uiTheme = nextTheme;
    refreshUiThemeButtons();

    try {
        localStorage.setItem(uiThemeStorageKey, nextTheme);
    } catch (error) {
        console.warn("Theme preference was not persisted.", error);
    }
}

function normalizeDeviceName(value) {
    return String(value ?? "")
        .replace(/\./g, " ")
        .replace(/[^A-Za-z0-9 _-]+/g, "")
        .replace(/\s+/g, " ")
        .trim()
        .slice(0, 31);
}

function defaultDeviceNameLabel() {
    return normalizeDeviceName(deviceIdentity.defaultDeviceName) || "SmartClock";
}

function buildHostnameFromDeviceName(value) {
    const effectiveName = normalizeDeviceName(value) || defaultDeviceNameLabel();
    const slug = effectiveName
        .toLowerCase()
        .replace(/[^a-z0-9 _-]+/g, "")
        .replace(/[\s_]+/g, "-")
        .replace(/-+/g, "-")
        .replace(/^-+|-+$/g, "");

    if (!slug) {
        return "smartclock";
    }

    if (slug === "smartclock") {
        const fallbackName = defaultDeviceNameLabel();
        if (fallbackName && normalizeDeviceName(fallbackName).toLowerCase() !== "smartclock") {
            return buildHostnameFromDeviceName(fallbackName);
        }
        return "smartclock";
    }

    if (slug.startsWith("smartclock-")) {
        return slug.slice(0, 31);
    }

    return `smartclock-${slug}`.slice(0, 31);
}

function updateDeviceMeta() {
    const deviceName = normalizeDeviceName(deviceIdentity.deviceName) || defaultDeviceNameLabel();
    const hostName = String(deviceIdentity.hostName || buildHostnameFromDeviceName(deviceName));
    const hostLabel = `${hostName}.local`;

    document.getElementById("deviceMeta").textContent = `${deviceName} · ${hostLabel}${deviceIdentity.rebootRequired ? " · Restart to apply new address" : ""}`;
    document.getElementById("authDeviceName").textContent = deviceName;
    document.getElementById("authDeviceHost").textContent = hostLabel;
}

function setDeviceIdentity(payload = {}) {
    const nextDefaultDeviceName = normalizeDeviceName(payload.defaultDeviceName) || deviceIdentity.defaultDeviceName || "SmartClock";
    const nextDeviceName = normalizeDeviceName(payload.deviceName) || nextDefaultDeviceName;
    const nextHostName = String(payload.hostName || buildHostnameFromDeviceName(nextDeviceName));

    deviceIdentity.defaultDeviceName = nextDefaultDeviceName;
    deviceIdentity.deviceName = nextDeviceName;
    deviceIdentity.hostName = nextHostName;
    deviceIdentity.rebootRequired = Boolean(payload.rebootRequired);
    updateDeviceMeta();
    updateDeviceIdentityPreview();
}

function updateDeviceIdentityPreview() {
    const input = document.getElementById("deviceName");
    const previewName = normalizeDeviceName(input ? input.value : "") ||
        normalizeDeviceName(deviceIdentity.deviceName) ||
        defaultDeviceNameLabel();
    const previewHost = buildHostnameFromDeviceName(previewName);

    document.getElementById("deviceNamePreview").textContent = previewName;
    document.getElementById("deviceHostPreview").textContent = `${previewHost}.local`;
}

function setSyncState(label, tone = "") {
    const element = document.getElementById("syncState");
    element.textContent = label;
    element.className = `pill ${tone}`.trim();
}

function setAuthMessage(message = "", tone = "") {
    const element = document.getElementById("authMessage");
    element.textContent = message;
    element.className = `auth-message ${tone}`.trim();
}

function updateAuthGuidance() {
    const revealButton = document.getElementById("authRevealButton");
    const setupNote = document.getElementById("authSetupNote");
    const recoveryNote = document.getElementById("authRecoveryNote");

    revealButton.classList.toggle("hidden", !authCanRevealPassword);

    if (authCanRevealPassword) {
        document.getElementById("authHint").textContent = authProvisionedThisBoot
            ? "A generated setup password was created on this boot. If you missed it, show it on the device screen."
            : "This device is still using its generated setup password. You can show it on the device screen.";
        setupNote.innerHTML = "<strong>First install or reset</strong>Connect to <code>SmartClock-Setup</code>, open <code>192.168.4.1</code>, then tap <code>Show password on device</code> if you missed the setup password.";
        recoveryNote.innerHTML = "<strong>Security model</strong>The reveal action only works while the generated setup password is still active. After you replace it, the device keeps only a secure hash.";
        return;
    }

    document.getElementById("authHint").textContent = "Sign in to manage the device.";
    setupNote.innerHTML = "<strong>First install or reset</strong>Connect to <code>SmartClock-Setup</code> and open <code>192.168.4.1</code>. New devices provisioned by this firmware can reveal the generated setup password from this page.";
    recoveryNote.innerHTML = "<strong>Forgot a password?</strong>Custom or older passwords cannot be displayed because only a secure hash is stored. Use 5 quick power cycles to factory reset, then sign in with the new generated setup password.";
}

function setAuthenticatedState(nextState) {
    authenticated = Boolean(nextState);
    document.body.classList.toggle("auth-open", !authenticated);
    document.getElementById("authOverlay").classList.toggle("active", !authenticated);
    document.getElementById("signOutButton").classList.toggle("hidden", !authenticated);
    updateActionButtons();
}

function showAuthOverlay(message = "", tone = "") {
    setAuthenticatedState(false);
    setAuthMessage(message, tone);
    document.getElementById("authPassword").value = "";
    document.getElementById("authPassword").focus();
}

function hideAuthOverlay() {
    setAuthenticatedState(true);
    setAuthMessage("");
}

function handleUnauthorized(message = "Sign in to continue.") {
    hydrating = false;
    showAuthOverlay(message, "error");
    setSyncState("Locked", "error");
    updateActionButtons();
}

async function request(url, options = {}) {
    const response = await fetch(url, options);
    if (response.status === 401 && url !== "/auth/login" && url !== "/auth/status") {
        handleUnauthorized("Session expired. Sign in again.");
    }
    return response;
}

async function postJson(url, body) {
    return request(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
    });
}

async function refreshAuthStatus() {
    const response = await fetch("/auth/status");
    if (!response.ok) {
        throw new Error("Auth status failed");
    }

    const authState = await response.json();
    setDeviceIdentity(authState);
    authProvisionedThisBoot = Boolean(authState.provisionedThisBoot);
    authCanRevealPassword = Boolean(authState.canRevealPassword);
    document.getElementById("authUsernameLabel").textContent = authState.username || "admin";
    document.getElementById("securityStatus").textContent = authState.authenticated
        ? (authCanRevealPassword
            ? `Signed in as ${authState.username || "admin"}. Setup password reveal stays available until you replace it.`
            : `Signed in as ${authState.username || "admin"}.`)
        : (authCanRevealPassword
            ? `Admin user: ${authState.username || "admin"}. Setup password reveal is available from the login screen.`
            : `Admin user: ${authState.username || "admin"}.`);
    updateAuthGuidance();

    if (authState.authenticated) {
        hideAuthOverlay();
    } else {
        showAuthOverlay(authCanRevealPassword ? "Use the reveal button if you need the setup password." : "");
    }
}

function updateActionButtons() {
    const locked = hydrating || !authenticated;
    document.getElementById("saveDisplayButton").disabled = locked || !dirty;
    document.getElementById("discardDisplayButton").disabled = locked || !dirty;
    document.getElementById("saveFeedsButton").disabled = locked || !feedDirty;
    document.getElementById("discardFeedsButton").disabled = locked || !feedDirty;
    document.getElementById("saveHomeAssistantButton").disabled = locked || !feedDirty;
    document.getElementById("discardHomeAssistantButton").disabled = locked || !feedDirty;
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes) || bytes < 0) {
        return "--";
    }
    if (bytes < 1024) {
        return `${bytes} B`;
    }
    if (bytes < 1024 * 1024) {
        return `${(bytes / 1024).toFixed(1)} KB`;
    }
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function formatAge(ageSec) {
    if (!Number.isFinite(ageSec) || ageSec < 0) {
        return "--";
    }
    if (ageSec < 15) {
        return "Just now";
    }
    if (ageSec < 3600) {
        return `${Math.max(1, Math.round(ageSec / 60))}m ago`;
    }
    return `${Math.round(ageSec / 3600)}h ago`;
}

function sourceLabel(source) {
    if (source === "open-meteo") {
        return "Open-Meteo";
    }
    if (source === "coingecko") {
        return "CoinGecko";
    }
    if (source === "finnhub") {
        return "Finnhub";
    }
    return "Disabled";
}

function normalizeWeatherSourceForUI(source) {
    return source === "open-meteo" ? "open-meteo" : "disabled";
}

function normalizeMarketSourceForUI(source) {
    return source === "finnhub" || source === "coingecko" ? source : "disabled";
}

function normalizeHomeAssistantTlsMode(mode) {
    return mode === "fingerprint" ? "fingerprint" : "insecure";
}

function homeAssistantHostLabel() {
    const raw = document.getElementById("homeAssistantBaseUrl").value.trim();
    if (!raw) {
        return "Home Assistant";
    }

    try {
        return new URL(raw).host || raw;
    } catch (error) {
        return raw.replace(/^https?:\/\//i, "").replace(/\/.*$/, "") || "Home Assistant";
    }
}

function normalizeHexColor(value, fallback) {
    const base = (value || "").trim().replace(/^#/, "").toUpperCase();
    if (/^[0-9A-F]{6}$/.test(base)) {
        return `#${base}`;
    }
    return fallback;
}

function readNumber(id, fallback = 0) {
    const value = parseFloat(document.getElementById(id).value);
    return Number.isFinite(value) ? value : fallback;
}

function readInt(id, fallback = 0) {
    const value = parseInt(document.getElementById(id).value, 10);
    return Number.isFinite(value) ? value : fallback;
}

function setValue(id, value) {
    document.getElementById(id).value = value ?? "";
}

function setChecked(id, value) {
    document.getElementById(id).checked = Boolean(value);
}

function ensurePageSelection(preferredId = "pageClock") {
    const anyEnabled = dashboardPageControls.some(({ id }) => document.getElementById(id).checked);
    if (anyEnabled) {
        return;
    }

    const fallbackId = preferredId || "pageClock";
    setChecked(fallbackId, true);
}

function applyEnabledPages(pages = {}) {
    const hasConfiguredPages = dashboardPageControls.some(({ key }) =>
        Object.prototype.hasOwnProperty.call(pages, key)
    );
    dashboardPageControls.forEach(({ key, id }) => {
        const enabled = hasConfiguredPages ? Boolean(pages[key]) : true;
        setChecked(id, enabled);
    });
    ensurePageSelection();
}

function readEnabledPages() {
    const pages = {};
    dashboardPageControls.forEach(({ key, id }) => {
        pages[key] = document.getElementById(id).checked;
    });
    ensurePageSelection();
    pages.clock = document.getElementById("pageClock").checked;
    return pages;
}

function defaultThemeColors(prefix = "custom") {
    return prefix === "nightCustom" ? defaultNightCustomTheme : defaultCustomTheme;
}

function themeColorControlIds(name, prefix = "custom") {
    const controlPrefix = `${prefix}${name.charAt(0).toUpperCase() + name.slice(1)}`;
    return {
        picker: `${controlPrefix}Picker`,
        hex: `${controlPrefix}Hex`
    };
}

function setThemeColorControl(name, value, prefix = "custom") {
    const fallback = defaultThemeColors(prefix)[name];
    const normalized = normalizeHexColor(value, fallback);
    const ids = themeColorControlIds(name, prefix);
    setValue(ids.hex, normalized);
    setValue(ids.picker, normalized);
}

function getThemeColorValue(name, prefix = "custom") {
    const defaults = defaultThemeColors(prefix);
    const ids = themeColorControlIds(name, prefix);
    const pickerValue = normalizeHexColor(document.getElementById(ids.picker).value, defaults[name]);
    return normalizeHexColor(document.getElementById(ids.hex).value, pickerValue);
}

function bindThemeColorInputs(prefix = "custom") {
    const defaults = defaultThemeColors(prefix);
    ["accent", "background", "surface", "text"].forEach((name) => {
        const ids = themeColorControlIds(name, prefix);
        const picker = document.getElementById(ids.picker);
        const hex = document.getElementById(ids.hex);

        picker.addEventListener("input", () => {
            hex.value = normalizeHexColor(picker.value, defaults[name]);
        });

        hex.addEventListener("input", () => {
            const normalized = normalizeHexColor(hex.value, "");
            if (normalized) {
                picker.value = normalized;
            }
        });

        hex.addEventListener("blur", () => {
            const normalized = getThemeColorValue(name, prefix);
            hex.value = normalized;
            picker.value = normalized;
        });
    });
}

function formatMinutesOfDay(totalMinutes) {
    const safeMinutes = Number.isFinite(totalMinutes) ? totalMinutes : 0;
    const normalized = ((safeMinutes % 1440) + 1440) % 1440;
    const hours = String(Math.floor(normalized / 60)).padStart(2, "0");
    const minutes = String(normalized % 60).padStart(2, "0");
    return `${hours}:${minutes}`;
}

function parseTimeOfDay(value, fallbackMinutes = 0) {
    const match = /^(\d{2}):(\d{2})$/.exec(String(value || "").trim());
    if (!match) {
        return fallbackMinutes;
    }

    const hours = parseInt(match[1], 10);
    const minutes = parseInt(match[2], 10);
    if (!Number.isFinite(hours) || !Number.isFinite(minutes) || hours > 23 || minutes > 59) {
        return fallbackMinutes;
    }

    return (hours * 60) + minutes;
}

function updateCustomThemeUI() {
    const enabled = document.getElementById("customThemeEnabled").checked;
    const fields = document.getElementById("customThemeFields");
    fields.classList.toggle("hidden", !enabled);
    fields.querySelectorAll("input").forEach((input) => {
        input.disabled = !enabled;
    });
}

function populateTimezoneOptions() {
    const timezoneSelect = document.getElementById("timezonePreset");
    timezoneSelect.innerHTML = "";

    timezoneOptions.forEach((optionData) => {
        const option = document.createElement("option");
        option.value = String(optionData.value);
        option.textContent = optionData.label;
        timezoneSelect.appendChild(option);
    });

    const customOption = document.createElement("option");
    customOption.value = "custom";
    customOption.textContent = "Custom";
    timezoneSelect.appendChild(customOption);
}

function populateOffsetSelect(selectId) {
    const select = document.getElementById(selectId);
    select.innerHTML = "";
    timezoneOptions.forEach((optionData) => {
        const option = document.createElement("option");
        option.value = String(optionData.value);
        option.textContent = optionData.label;
        select.appendChild(option);
    });
}

function populateThemeButtons(containerId, inputId) {
    const container = document.getElementById(containerId);
    container.innerHTML = "";

    themeLabels.forEach((themeData) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "theme-button";
        button.dataset.targetInput = inputId;
        button.dataset.theme = String(themeData.value);

        const preview = document.createElement("span");
        preview.className = "theme-preview";
        (themeData.tones || []).forEach((tone) => {
            const swatch = document.createElement("span");
            swatch.className = "theme-swatch";
            swatch.style.background = tone;
            preview.appendChild(swatch);
        });

        const label = document.createElement("span");
        label.className = "theme-button-title";
        label.textContent = themeData.label;

        button.appendChild(preview);
        button.appendChild(label);
        button.addEventListener("click", () => {
            document.getElementById(inputId).value = String(themeData.value);
            updateThemeButtons(containerId, inputId);
            scheduleLiveSync(90);
        });
        container.appendChild(button);
    });
}

function updateThemeButtons(containerId, inputId) {
    const activeTheme = String(document.getElementById(inputId).value || "0");
    document.querySelectorAll(`#${containerId} .theme-button`).forEach((button) => {
        button.classList.toggle("active", button.dataset.theme === activeTheme);
        button.setAttribute("aria-pressed", button.dataset.theme === activeTheme ? "true" : "false");
    });
}

function handleTimezonePresetChange() {
    const preset = document.getElementById("timezonePreset").value;
    const customWrap = document.getElementById("timezoneCustomWrap");
    if (preset === "custom") {
        customWrap.classList.remove("hidden");
    } else {
        customWrap.classList.add("hidden");
        document.getElementById("timezoneCustom").value = preset;
    }
}

function applyTimezoneToUI(gmtOffset) {
    const preset = document.getElementById("timezonePreset");
    const custom = document.getElementById("timezoneCustom");
    const match = timezoneOptions.find((optionData) => optionData.value === gmtOffset);

    if (match) {
        preset.value = String(gmtOffset);
        custom.value = String(gmtOffset);
    } else {
        preset.value = "custom";
        custom.value = String(gmtOffset);
    }

    handleTimezonePresetChange();
}

function getTimezoneFromUI() {
    const preset = document.getElementById("timezonePreset").value;
    if (preset === "custom") {
        const customValue = parseInt(document.getElementById("timezoneCustom").value, 10);
        return Number.isFinite(customValue) ? customValue : null;
    }

    const presetValue = parseInt(preset, 10);
    return Number.isFinite(presetValue) ? presetValue : null;
}

function updateBrightnessLabel() {
    document.getElementById("brightnessValue").textContent = `${readInt("brightness", 70)}%`;
}

function updateNightBrightnessLabel() {
    const value = readInt("nightBrightness", 14);
    document.getElementById("nightBrightnessValue").textContent = `${Math.max(1, value)}%`;
}

function updateNightThemeUI() {
    const nightEnabled = document.getElementById("nightModeEnabled").checked;
    const themeEnabled = nightEnabled && document.getElementById("nightThemeEnabled").checked;
    const customEnabled = themeEnabled && document.getElementById("nightCustomThemeEnabled").checked;
    const presetWrap = document.getElementById("nightThemePresetWrap");
    const customToggle = document.getElementById("nightCustomThemeToggle");
    const customFields = document.getElementById("nightCustomThemeFields");

    presetWrap.classList.toggle("hidden", !themeEnabled);
    customToggle.classList.toggle("hidden", !themeEnabled);
    customFields.classList.toggle("hidden", !customEnabled);

    presetWrap.querySelectorAll("button, input").forEach((element) => {
        element.disabled = !themeEnabled;
    });
    customToggle.querySelectorAll("input").forEach((element) => {
        element.disabled = !themeEnabled;
    });
    customFields.querySelectorAll("input").forEach((element) => {
        element.disabled = !customEnabled;
    });
}

function updateNightModeUI() {
    const enabled = document.getElementById("nightModeEnabled").checked;
    const fields = document.getElementById("nightModeFields");

    fields.classList.toggle("hidden", !enabled);
    fields.querySelectorAll("input, button").forEach((element) => {
        element.disabled = !enabled;
    });

    updateNightBrightnessLabel();
    updateNightThemeUI();
}

function applyAppState(appState) {
    setDeviceIdentity(appState);
    setValue("deviceName", appState.deviceName ?? appState.defaultDeviceName ?? "");
    setValue("brightness", appState.brt ?? 70);
    setValue("imagePath", appState.img ?? "");
    applyTimezoneToUI(typeof appState.gmtOffset === "number" ? appState.gmtOffset : 0);
    updateBrightnessLabel();
    updateDeviceIdentityPreview();
}

function applyDashboardState(dashboardState) {
    const config = dashboardState.config || {};
    const data = dashboardState.data || {};

    document.getElementById("themePreset").value = String(config.theme ?? 0);
    updateThemeButtons("themePicker", "themePreset");
    setChecked("customThemeEnabled", config.customThemeEnabled);

    const customTheme = config.customTheme || {};
    setThemeColorControl("accent", customTheme.accent || defaultCustomTheme.accent);
    setThemeColorControl("background", customTheme.background || defaultCustomTheme.background);
    setThemeColorControl("surface", customTheme.surface || defaultCustomTheme.surface);
    setThemeColorControl("text", customTheme.text || defaultCustomTheme.text);
    updateCustomThemeUI();

    const nightMode = config.nightMode || {};
    setChecked("nightModeEnabled", nightMode.enabled);
    setValue("nightStart", formatMinutesOfDay(nightMode.startMinutes ?? (22 * 60)));
    setValue("nightEnd", formatMinutesOfDay(nightMode.endMinutes ?? (7 * 60)));
    setValue("nightBrightness", Math.max(1, nightMode.brightness ?? 14));
    setChecked("nightThemeEnabled", nightMode.themeEnabled);
    document.getElementById("nightThemePreset").value = String(nightMode.theme ?? 0);
    updateThemeButtons("nightThemePicker", "nightThemePreset");
    setChecked("nightCustomThemeEnabled", nightMode.customThemeEnabled);

    const nightCustomTheme = nightMode.customTheme || {};
    setThemeColorControl("accent", nightCustomTheme.accent || defaultNightCustomTheme.accent, "nightCustom");
    setThemeColorControl("background", nightCustomTheme.background || defaultNightCustomTheme.background, "nightCustom");
    setThemeColorControl("surface", nightCustomTheme.surface || defaultNightCustomTheme.surface, "nightCustom");
    setThemeColorControl("text", nightCustomTheme.text || defaultNightCustomTheme.text, "nightCustom");
    updateNightModeUI();

    setValue("rotationInterval", config.rotationIntervalSec ?? 10);
    setChecked("rotationEnabled", config.rotationEnabled);
    setChecked("use24Hour", config.use24Hour);
    setChecked("showSeconds", config.showSeconds);
    setChecked("showIp", config.showIp);
    applyEnabledPages(config.pages || {});

    const focus = data.focus || {};
    setValue("focusLabel", focus.label || "");
    setValue("focusDuration", focus.durationMinutes ?? 25);
    setValue("focusRemaining", Math.round((focus.remainingSeconds ?? 1500) / 60));
    setChecked("focusRunning", focus.running);
    setChecked("focusBreakMode", focus.breakMode);

    const worldClocks = data.worldClocks || [];
    for (let index = 0; index < 2; index += 1) {
        const clock = worldClocks[index] || {};
        setChecked(`worldEnabled${index}`, clock.enabled);
        setValue(`worldLabel${index}`, clock.label || "");
        setValue(`worldOffset${index}`, clock.offsetSeconds ?? 0);
    }

    const eventData = data.event || {};
    setValue("eventTitle", eventData.title || "");
    setValue("eventSubtitle", eventData.subtitle || "");
    setValue("eventCountdown", Math.round((eventData.remainingSeconds ?? 0) / 60));

    const quote = data.quote || {};
    setValue("quoteText", quote.text || "");
    setValue("quoteAuthor", quote.author || "");

    const status = data.status || {};
    setValue("statusLine1", status.line1 || "");
    setValue("statusLine2", status.line2 || "");
    widgetBaseline = buildWidgetSnapshot();
}

function updateWeatherModeUI() {
    const source = normalizeWeatherSourceForUI(document.getElementById("weatherSource").value);
    const weatherSource = document.getElementById("weatherSource");
    const autoFields = document.getElementById("weatherAutoFields");
    weatherSource.value = source;
    autoFields.classList.toggle("hidden", source !== "open-meteo");
    document.getElementById("weatherUseFahrenheit").disabled = source !== "open-meteo";
    document.getElementById("weatherFeedBadge").textContent = sourceLabel(source);

    autoFields.querySelectorAll("input, button").forEach((element) => {
        element.disabled = source !== "open-meteo";
    });

    if (source !== "open-meteo") {
        document.getElementById("weatherResults").innerHTML = "";
        document.getElementById("weatherSearchStatus").textContent = "Weather feed disabled.";
    } else if (!document.getElementById("weatherSearchStatus").textContent.trim() ||
               document.getElementById("weatherSearchStatus").textContent === "Weather feed disabled.") {
        document.getElementById("weatherSearchStatus").textContent = "Search a city or set coordinates directly.";
    }
}

function updateMarketModeUI(index) {
    const select = document.getElementById(`marketSource${index}`);
    const source = normalizeMarketSourceForUI(select.value);
    const currencyField = document.getElementById(`feedMarketCurrency${index}`);
    const symbolField = document.getElementById(`feedMarketSymbol${index}`);
    const labelField = document.getElementById(`feedMarketLabel${index}`);
    const refreshField = document.getElementById(`feedMarketRefresh${index}`);
    const hint = document.getElementById(`marketHint${index}`);
    const meta = document.getElementById(`marketSlotMeta${index}`);

    select.value = source;
    symbolField.disabled = source === "disabled";
    labelField.disabled = source === "disabled";
    refreshField.disabled = source === "disabled";
    currencyField.disabled = source !== "coingecko";
    meta.textContent = sourceLabel(source);

    if (source === "finnhub") {
        hint.textContent = "Finnhub uses exchange tickers and needs an API key.";
    } else if (source === "coingecko") {
        hint.textContent = "CoinGecko accepts IDs, names, or symbols. Sync resolves the best match.";
    } else {
        hint.textContent = "Disabled slots are ignored on the market page.";
    }
}

function updateHomeAssistantModeUI() {
    const enabled = document.getElementById("homeAssistantEnabled").checked;
    const tlsMode = normalizeHomeAssistantTlsMode(document.getElementById("homeAssistantTlsMode").value);
    const fields = document.getElementById("homeAssistantFields");
    const fingerprintField = document.getElementById("homeAssistantFingerprintField");
    const syncButton = document.getElementById("homeAssistantSyncButton");

    document.getElementById("homeAssistantTlsMode").value = tlsMode;
    fields.classList.toggle("hidden", !enabled);
    fields.querySelectorAll("input, select, button").forEach((element) => {
        element.disabled = !enabled;
    });

    fingerprintField.classList.toggle("hidden", !enabled || tlsMode !== "fingerprint");
    fingerprintField.querySelectorAll("input").forEach((element) => {
        element.disabled = !enabled || tlsMode !== "fingerprint";
    });
    syncButton.disabled = !enabled;

    document.getElementById("homeAssistantFeedBadge").textContent = enabled ? homeAssistantHostLabel() : "Disabled";

    for (let index = 0; index < 4; index += 1) {
        const slotEnabled = enabled && document.getElementById(`haEnabled${index}`).checked;
        document.getElementById(`haEntityId${index}`).disabled = !slotEnabled;
        document.getElementById(`haLabel${index}`).disabled = !slotEnabled;
        document.getElementById(`haUnit${index}`).disabled = !slotEnabled;
        if (!enabled) {
            document.getElementById(`haSlotMeta${index}`).textContent = "Disabled";
        } else if (!slotEnabled) {
            document.getElementById(`haSlotMeta${index}`).textContent = "Off";
        } else if (!document.getElementById(`haSlotMeta${index}`).textContent.trim() ||
                   document.getElementById(`haSlotMeta${index}`).textContent === "Disabled" ||
                   document.getElementById(`haSlotMeta${index}`).textContent === "Off") {
            document.getElementById(`haSlotMeta${index}`).textContent =
                document.getElementById(`haEntityId${index}`).value.trim() || "Waiting";
        }
    }
}

function feedStatusText(source, status, fallbackText) {
    if (source === "disabled") {
        return "Disabled";
    }
    if (status?.syncing) {
        return "Syncing";
    }
    if (status?.lastError) {
        return status.lastError.length > 24 ? `${status.lastError.slice(0, 23)}...` : status.lastError;
    }
    if (status?.hasData) {
        return `${status.stale ? "Stale · " : ""}${formatAge(status.lastSuccessAgeSec)}`;
    }
    return fallbackText;
}

function applyFeedState(feedState) {
    const config = feedState.config || {};
    const providers = config.providers || {};
    const weather = config.weather || {};
    const markets = config.markets || [];
    const homeAssistant = config.homeAssistant || {};
    const status = feedState.status || {};
    const weatherStatus = status.weather || {};
    const marketStatuses = status.markets || [];
    const homeAssistantStatus = status.homeAssistant || {};

    setValue("finnhubApiKey", providers.finnhubApiKey || "");

    const weatherSource = normalizeWeatherSourceForUI(weather.source || "disabled");
    setValue("weatherSource", weatherSource);
    setValue("weatherQuery", weather.query || "");
    setValue("weatherLabel", weather.label || "");
    setValue("weatherLatitude", weather.latitude ?? "");
    setValue("weatherLongitude", weather.longitude ?? "");
    setValue("weatherRefresh", weather.refreshMinutes ?? 30);
    setChecked("weatherUseFahrenheit", weather.useFahrenheit);
    updateWeatherModeUI();

    document.getElementById("weatherFeedStatus").textContent = feedStatusText(
        weatherSource,
        weatherStatus,
        "Waiting"
    );

    if (weatherStatus?.hasData && weatherStatus.data) {
        const label = weatherStatus.data.location || weather.label || weather.query || "Weather";
        document.getElementById("weatherFeedBadge").textContent = label;
    }

    for (let index = 0; index < 3; index += 1) {
        const market = markets[index] || {};
        const marketStatus = marketStatuses[index] || {};
        const marketSource = normalizeMarketSourceForUI(market.source || "disabled");

        setValue(`marketSource${index}`, marketSource);
        setValue(`feedMarketSymbol${index}`, market.symbol || "");
        setValue(`feedMarketLabel${index}`, market.label || "");
        setValue(`feedMarketCurrency${index}`, market.currency || "usd");
        setValue(`feedMarketRefresh${index}`, market.refreshMinutes ?? 10);
        updateMarketModeUI(index);

        document.getElementById(`marketFeedStatus${index}`).textContent = feedStatusText(
            marketSource,
            marketStatus,
            "Waiting"
        );
    }

    setChecked("homeAssistantEnabled", homeAssistant.enabled);
    setValue("homeAssistantBaseUrl", homeAssistant.baseUrl || "");
    setValue("homeAssistantToken", homeAssistant.token || "");
    setValue("homeAssistantTlsMode", normalizeHomeAssistantTlsMode(homeAssistant.tlsMode || "insecure"));
    setValue("homeAssistantFingerprint", homeAssistant.fingerprint || "");
    setValue("homeAssistantRefresh", homeAssistant.refreshMinutes ?? 2);

    const homeSlots = homeAssistant.slots || [];
    const homeSlotStatuses = homeAssistantStatus.slots || [];
    for (let index = 0; index < 4; index += 1) {
        const slot = homeSlots[index] || {};
        const slotStatus = homeSlotStatuses[index] || {};
        setChecked(`haEnabled${index}`, slot.enabled);
        setValue(`haEntityId${index}`, slot.entityId || "");
        setValue(`haLabel${index}`, slot.label || "");
        setValue(`haUnit${index}`, slot.unit || "");

        if (!homeAssistant.enabled) {
            document.getElementById(`haSlotMeta${index}`).textContent = "Disabled";
        } else if (!slot.enabled) {
            document.getElementById(`haSlotMeta${index}`).textContent = "Off";
        } else if (slotStatus?.hasData) {
            const stateLabel = [slotStatus.state, slotStatus.unit].filter(Boolean).join(" ");
            document.getElementById(`haSlotMeta${index}`).textContent = [
                stateLabel || "Ready",
                slotStatus.lastError ? `Stale: ${slotStatus.lastError}` : (slotStatus.stale ? "Stale" : ""),
                formatAge(slotStatus.lastSuccessAgeSec)
            ].filter(Boolean).join(" · ");
        } else if (slotStatus?.lastError) {
            document.getElementById(`haSlotMeta${index}`).textContent = slotStatus.lastError;
        } else {
            document.getElementById(`haSlotMeta${index}`).textContent = slot.entityId || "Waiting";
        }
    }

    updateHomeAssistantModeUI();
    document.getElementById("homeAssistantFeedStatus").textContent = feedStatusText(
        homeAssistant.enabled ? "home" : "disabled",
        homeAssistantStatus,
        "Waiting"
    );
}

function buildSettingsPayload() {
    return {
        deviceName: document.getElementById("deviceName").value.trim(),
        brightness: readInt("brightness", 70),
        gmtOffset: getTimezoneFromUI()
    };
}

function buildConfigPayload() {
    const customTheme = {
        background: getThemeColorValue("background"),
        surface: getThemeColorValue("surface"),
        accent: getThemeColorValue("accent"),
        text: getThemeColorValue("text")
    };

    const nightMode = {
        enabled: document.getElementById("nightModeEnabled").checked,
        startMinutes: parseTimeOfDay(document.getElementById("nightStart").value, 22 * 60),
        endMinutes: parseTimeOfDay(document.getElementById("nightEnd").value, 7 * 60),
        brightness: Math.max(1, readInt("nightBrightness", 14)),
        themeEnabled: document.getElementById("nightThemeEnabled").checked,
        theme: readInt("nightThemePreset", 0),
        customThemeEnabled: document.getElementById("nightCustomThemeEnabled").checked,
        customTheme: {
            background: getThemeColorValue("background", "nightCustom"),
            surface: getThemeColorValue("surface", "nightCustom"),
            accent: getThemeColorValue("accent", "nightCustom"),
            text: getThemeColorValue("text", "nightCustom")
        }
    };

    return {
        theme: readInt("themePreset", 0),
        customThemeEnabled: document.getElementById("customThemeEnabled").checked,
        customTheme,
        nightMode,
        pages: readEnabledPages(),
        rotationEnabled: document.getElementById("rotationEnabled").checked,
        rotationIntervalSec: readInt("rotationInterval", 10),
        use24Hour: document.getElementById("use24Hour").checked,
        showSeconds: document.getElementById("showSeconds").checked,
        showIp: document.getElementById("showIp").checked
    };
}

function buildWidgetSnapshot() {
    return {
        focus: {
            label: document.getElementById("focusLabel").value.trim(),
            running: document.getElementById("focusRunning").checked,
            breakMode: document.getElementById("focusBreakMode").checked,
            durationMinutes: readInt("focusDuration", 25),
            remainingSeconds: readInt("focusRemaining", 25) * 60
        },
        worldClocks: [0, 1].map((index) => ({
            enabled: document.getElementById(`worldEnabled${index}`).checked,
            label: document.getElementById(`worldLabel${index}`).value.trim(),
            offsetSeconds: readInt(`worldOffset${index}`, 0)
        })),
        event: {
            title: document.getElementById("eventTitle").value.trim(),
            subtitle: document.getElementById("eventSubtitle").value.trim(),
            remainingSeconds: readInt("eventCountdown", 0) * 60
        },
        quote: {
            text: document.getElementById("quoteText").value.trim(),
            author: document.getElementById("quoteAuthor").value.trim()
        },
        status: {
            line1: document.getElementById("statusLine1").value.trim(),
            line2: document.getElementById("statusLine2").value.trim()
        }
    };
}

function buildWidgetPayload(snapshot = buildWidgetSnapshot()) {
    const changed = {};
    for (const [key, value] of Object.entries(snapshot)) {
        if (Array.isArray(value)) {
            if (JSON.stringify(value) !== JSON.stringify(widgetBaseline[key])) changed[key] = value;
        } else {
            const fields = {};
            for (const [field, next] of Object.entries(value)) {
                if (next !== widgetBaseline[key]?.[field]) fields[field] = next;
            }
            if (Object.keys(fields).length) changed[key] = fields;
        }
    }
    return changed;
}

function buildFeedPayload() {
    return {
        providers: {
            finnhubApiKey: document.getElementById("finnhubApiKey").value.trim()
        },
        weather: {
            source: document.getElementById("weatherSource").value,
            query: document.getElementById("weatherQuery").value.trim(),
            label: document.getElementById("weatherLabel").value.trim(),
            latitude: readNumber("weatherLatitude", 0),
            longitude: readNumber("weatherLongitude", 0),
            refreshMinutes: readInt("weatherRefresh", 30),
            useFahrenheit: document.getElementById("weatherUseFahrenheit").checked
        },
        markets: [0, 1, 2].map((index) => ({
            source: document.getElementById(`marketSource${index}`).value,
            symbol: document.getElementById(`feedMarketSymbol${index}`).value.trim(),
            label: document.getElementById(`feedMarketLabel${index}`).value.trim(),
            currency: document.getElementById(`feedMarketCurrency${index}`).value.trim().toLowerCase(),
            refreshMinutes: readInt(`feedMarketRefresh${index}`, 10)
        })),
        homeAssistant: {
            enabled: document.getElementById("homeAssistantEnabled").checked,
            baseUrl: document.getElementById("homeAssistantBaseUrl").value.trim(),
            token: document.getElementById("homeAssistantToken").value.trim(),
            tlsMode: document.getElementById("homeAssistantTlsMode").value,
            fingerprint: document.getElementById("homeAssistantFingerprint").value.trim(),
            refreshMinutes: readInt("homeAssistantRefresh", 2),
            slots: [0, 1, 2, 3].map((index) => ({
                enabled: document.getElementById(`haEnabled${index}`).checked,
                entityId: document.getElementById(`haEntityId${index}`).value.trim(),
                label: document.getElementById(`haLabel${index}`).value.trim(),
                unit: document.getElementById(`haUnit${index}`).value.trim()
            }))
        }
    };
}

function buildLivePayload() {
    return {
        settings: buildSettingsPayload(),
        config: buildConfigPayload(),
        data: buildWidgetPayload()
    };
}

function scheduleFeedPreview(delay = 180) {
    if (hydrating || !authenticated) {
        return;
    }

    feedDirty = true;
    feedEditGeneration += 1;
    setSyncState("Feeds pending", "pending");
    window.clearTimeout(feedLiveTimer);
    updateActionButtons();

    feedLiveTimer = window.setTimeout(() => {
        feedLiveTimer = 0;
        feedPromise = doFeedPreview();
    }, delay);
}

function scheduleLiveSync(delay = 180) {
    if (hydrating || !authenticated) {
        return;
    }

    dirty = true;
    editGeneration += 1;
    setSyncState("Pending", "pending");
    window.clearTimeout(liveTimer);
    updateActionButtons();

    liveTimer = window.setTimeout(() => {
        liveTimer = 0;
        livePromise = doLiveSync();
    }, delay);
}

async function doLiveSync() {
    return queueOperation(async () => {
        const timezone = getTimezoneFromUI();
        if (timezone === null) {
            setSyncState("Timezone", "error");
            throw new Error("Invalid timezone");
        }

        const payload = buildLivePayload();
        setSyncState("Preview", "pending");

        const response = await postJson("/dashboard/live", payload);

        if (!response.ok) {
            throw new Error(await response.text());
        }

        setSyncState("Display draft", "live");
    });
}

async function doFeedPreview() {
    return queueOperation(async () => {
        const payload = buildFeedPayload();
        setSyncState("Feeds preview", "pending");

        const response = await postJson("/feeds/live", payload);

        if (!response.ok) {
            throw new Error(await response.text());
        }

        setSyncState("Feeds draft", "live");
    });
}

async function flushLiveSync() {
    if (liveTimer) {
        window.clearTimeout(liveTimer);
        liveTimer = 0;
        livePromise = doLiveSync();
    }

    return livePromise;
}

async function flushFeedPreview() {
    if (feedLiveTimer) {
        window.clearTimeout(feedLiveTimer);
        feedLiveTimer = 0;
        feedPromise = doFeedPreview();
    }

    return feedPromise;
}

async function commitState() {
    if (hydrating || !dirty) {
        return;
    }

    try {
        await flushLiveSync();
        await queueOperation(async () => {
            setSyncState("Saving", "pending");

            const generation = editGeneration;
            const snapshot = buildWidgetSnapshot();
            const payload = buildLivePayload();
            const response = await postJson("/dashboard/save", payload);
            if (!response.ok) {
                throw new Error(await response.text());
            }

            widgetBaseline = snapshot;
            dirty = editGeneration !== generation;
            const appResponse = await request("/app.json");
            if (appResponse.ok) setDeviceIdentity(await appResponse.json());
            updateActionButtons();
            setSyncState(dirty ? "Pending" : "Saved", dirty ? "pending" : "saved");
        });
    } catch (error) {
        console.error(error);
        setSyncState("Save failed", "error");
    }
}

async function commitFeedConfig() {
    if (hydrating || !feedDirty) {
        return;
    }

    try {
        await flushFeedPreview();
        await queueOperation(async () => {
            setSyncState("Saving feeds", "pending");

            const generation = feedEditGeneration;
            const payload = buildFeedPayload();
            const response = await postJson("/feeds/save", payload);
            if (!response.ok) {
                throw new Error(await response.text());
            }

            feedDirty = feedEditGeneration !== generation;
            updateActionButtons();
            setSyncState(feedDirty ? "Feeds pending" : "Saved", feedDirty ? "pending" : "saved");
            if (!feedDirty) await loadFeedStateOnly();
        });
    } catch (error) {
        console.error(error);
        setSyncState("Feed save failed", "error");
    }
}

async function discardState() {
    if (hydrating || !dirty) {
        return;
    }

    window.clearTimeout(liveTimer);
    liveTimer = 0;

    const generation = editGeneration;
    try {
        await queueOperation(async () => {
            const response = await request("/dashboard/discard", { method: "POST" });
            if (!response.ok) {
                throw new Error(await response.text());
            }

            dirty = editGeneration !== generation;
            updateActionButtons();
            if (!dirty) await loadAllState();
            setSyncState("Discarded", "saved");
        });
    } catch (error) {
        console.error(error);
        setSyncState("Discard failed", "error");
    }
}

async function discardFeedConfig() {
    if (hydrating || !feedDirty) {
        return;
    }

    window.clearTimeout(feedLiveTimer);
    feedLiveTimer = 0;

    const generation = feedEditGeneration;
    try {
        await queueOperation(async () => {
            const response = await request("/feeds/discard", { method: "POST" });
            if (!response.ok) {
                throw new Error(await response.text());
            }

            feedDirty = feedEditGeneration !== generation;
            updateActionButtons();
            if (!feedDirty) await loadFeedStateOnly();
            setSyncState("Discarded", "saved");
        });
    } catch (error) {
        console.error(error);
        setSyncState("Discard failed", "error");
    }
}

async function resetDashboardDefaults() {
    if (!confirm("Reset layout and widget content?")) {
        return;
    }

    window.clearTimeout(liveTimer);
    liveTimer = 0;

    try {
        const response = await queueOperation(() => request("/dashboard/reset", { method: "POST" }));
        if (!response.ok) {
            throw new Error(await response.text());
        }
        dirty = false;
        updateActionButtons();
        await loadAllState();
        setSyncState("Saved", "saved");
    } catch (error) {
        console.error(error);
        setSyncState("Reset failed", "error");
    }
}

async function resetFeedDefaults() {
    if (!confirm("Reset feed providers and live source settings?")) {
        return;
    }

    window.clearTimeout(feedLiveTimer);
    feedLiveTimer = 0;

    try {
        const response = await queueOperation(() => request("/feeds/reset", { method: "POST" }));
        if (!response.ok) {
            throw new Error(await response.text());
        }
        feedDirty = false;
        updateActionButtons();
        await loadFeedStateOnly();
        setSyncState("Saved", "saved");
    } catch (error) {
        console.error(error);
        setSyncState("Feed reset failed", "error");
    }
}

function renderWeatherResults(results) {
    const container = document.getElementById("weatherResults");
    container.innerHTML = "";

    if (!Array.isArray(results) || results.length === 0) {
        document.getElementById("weatherSearchStatus").textContent = "No places found.";
        return;
    }

    results.forEach((result) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "search-result";
        const label = document.createElement("div");
        label.textContent = result.label || result.name || "Result";
        const meta = document.createElement("small");
        meta.textContent = `${result.timezone || "No timezone"} · ${result.latitude}, ${result.longitude}`;
        button.appendChild(label);
        button.appendChild(meta);
        button.addEventListener("click", () => {
            setValue("weatherQuery", result.name || "");
            setValue("weatherLabel", result.label || result.name || "");
            setValue("weatherLatitude", result.latitude ?? "");
            setValue("weatherLongitude", result.longitude ?? "");
            document.getElementById("weatherSearchStatus").textContent = `${result.label || result.name} selected.`;
            scheduleFeedPreview(40);
            syncFeeds("weather");
        });
        container.appendChild(button);
    });
}

async function searchWeatherLocations() {
    const query = document.getElementById("weatherQuery").value.trim();
    if (query.length < 2) {
        document.getElementById("weatherSearchStatus").textContent = "Enter at least 2 characters.";
        setSyncState("Search query", "error");
        return;
    }

    document.getElementById("weatherSearchStatus").textContent = "Searching...";
    document.getElementById("weatherResults").innerHTML = "";

    try {
        const response = await request(`/feeds/search?query=${encodeURIComponent(query)}`);
        if (!response.ok) {
            const message = (await response.text()).trim();
            throw new Error(message || "Search failed");
        }

        const payload = await response.json();
        renderWeatherResults(payload.results || []);
        setSyncState("Search ready", "saved");
    } catch (error) {
        console.error(error);
        const message = error instanceof Error && error.message ? error.message : "Search failed";
        document.getElementById("weatherSearchStatus").textContent = message;
        setSyncState("Search failed", "error");
    }
}

async function syncFeeds(scope = "all") {
    try {
        await flushFeedPreview();
        setSyncState("Syncing feeds", "pending");
        const response = await request(`/feeds/sync?scope=${encodeURIComponent(scope)}`, { method: "POST" });
        if (!response.ok) {
            const message = (await response.text()).trim();
            throw new Error(message || "Feed sync failed");
        }
        const feedState = await response.json();
        applyFeedState(feedState);
        feedDirty = Boolean(feedState.meta?.hasDraft);
        updateActionButtons();
        setSyncState(feedDirty ? "Feeds synced, draft open" : "Feeds synced", feedDirty ? "live" : "saved");
    } catch (error) {
        console.error(error);
        try {
            await loadFeedStateOnly();
        } catch (loadError) {
            console.error(loadError);
        }
        const message = error instanceof Error && error.message ? error.message : "Sync failed";
        if (scope === "weather") {
            document.getElementById("weatherSearchStatus").textContent = message;
        }
        setSyncState(message.length <= 24 ? message : "Sync failed", "error");
    }
}

async function uploadImage() {
    const fileInput = document.getElementById("fileUpload");
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
        document.getElementById("uploadStatus").textContent = "Choose a JPEG first.";
        setSyncState("Pick image", "error");
        return;
    }

    try {
        const formData = new FormData();
        formData.append("file", file);
        setSyncState("Uploading", "pending");

        const response = await request("/image/upload", {
            method: "POST",
            body: formData
        });
        const message = await response.text();
        if (!response.ok) {
            throw new Error(message || "Upload failed");
        }

        document.getElementById("uploadStatus").textContent = "Upload complete.";
        if (message && message.startsWith("/image/")) {
            document.getElementById("imagePath").value = message;
        }
        fileInput.value = "";
        setSyncState("Uploaded", "saved");
    } catch (error) {
        console.error(error);
        document.getElementById("uploadStatus").textContent = "Upload failed.";
        setSyncState("Upload failed", "error");
    }
}

function displayImage() {
    const imagePath = document.getElementById("imagePath").value.trim();
    postJson("/image/show", { path: imagePath })
        .then((response) => {
            if (!response.ok) {
                return response.text().then((message) => {
                    throw new Error(message || "Image update failed");
                });
            }
            setSyncState("Image", "saved");
        })
        .catch((error) => {
            console.error(error);
            setSyncState("Image failed", "error");
        });
}

function previewTestCard() {
    request("/test", { method: "POST" })
        .then((response) => {
            if (!response.ok) {
                throw new Error("Test failed");
            }
            setSyncState("Preview", "live");
        })
        .catch((error) => {
            console.error(error);
            setSyncState("Test failed", "error");
        });
}

function reconfigureWiFi() {
    if (!confirm("Clear saved WiFi and restart into AP mode?")) {
        return;
    }

    request("/reconfigurewifi", { method: "POST" })
        .then((response) => response.text().then((message) => ({ ok: response.ok, message })))
        .then(({ ok, message }) => {
            if (!ok) {
                throw new Error(message || "Portal failed");
            }
            setSyncState("Restarting AP", "pending");
        })
        .catch((error) => {
            console.error(error);
            setSyncState("Portal failed", "error");
        });
}

function factoryReset() {
    if (!confirm("Factory reset the device?")) {
        return;
    }

    request("/factoryreset", { method: "POST" })
        .then((response) => response.text().then((message) => ({ ok: response.ok, message })))
        .then(({ ok, message }) => {
            if (!ok) {
                throw new Error(message || "Reset failed");
            }
            setSyncState("Resetting", "pending");
        })
        .catch((error) => {
            console.error(error);
            setSyncState("Reset failed", "error");
        });
}

function scanWiFi() {
    const scanStatus = document.getElementById("scanStatus");
    scanStatus.textContent = "Scanning…";

    request("/scan")
        .then((response) => {
            if (!response.ok) {
                throw new Error("Scan failed");
            }
            return response.json();
        })
        .then((networks) => {
            const select = document.getElementById("wifiNetwork");
            select.innerHTML = '<option value="">Select…</option>';

            if (!Array.isArray(networks) || networks.length === 0) {
                scanStatus.textContent = "No networks";
                return;
            }

            networks
                .sort((left, right) => right.rssi - left.rssi)
                .forEach((network) => {
                    if (!network.ssid || !network.ssid.trim()) {
                        return;
                    }

                    const option = document.createElement("option");
                    option.value = network.ssid;
                    option.textContent = `${network.ssid} (${network.rssi} dBm)`;
                    select.appendChild(option);
                });

            scanStatus.textContent = `${networks.length} found`;
        })
        .catch((error) => {
            console.error(error);
            scanStatus.textContent = "Scan failed";
        });
}

function connectWiFi() {
    const ssid = document.getElementById("wifiNetwork").value;
    const password = document.getElementById("wifiPassword").value;

    if (!ssid) {
        setSyncState("Pick network", "error");
        return;
    }

    if (!confirm(`Connect to ${ssid}?`)) {
        return;
    }

    postJson("/connect", { ssid, password })
        .then((response) => response.text().then((message) => ({ ok: response.ok, message })))
        .then(({ ok, message }) => {
            if (!ok) {
                throw new Error(message || "Connect failed");
            }
            setSyncState("Connecting", "pending");
        })
        .then(() => {
            document.getElementById("wifiPassword").value = "";
        })
        .catch((error) => {
            console.error(error);
            setSyncState("Connect failed", "error");
        });
}

async function logoutSession() {
    try {
        await request("/auth/logout", { method: "POST" });
    } catch (error) {
        console.error(error);
    } finally {
        handleUnauthorized("Signed out.");
    }
}

async function revealPasswordOnDevice() {
    if (!authCanRevealPassword) {
        setAuthMessage("Custom or older passwords cannot be displayed.", "error");
        return;
    }

    const button = document.getElementById("authRevealButton");
    button.disabled = true;

    try {
        setAuthMessage("Showing password on device...");
        const response = await request("/auth/reveal", { method: "POST" });
        const message = await response.text();
        if (!response.ok) {
            throw new Error(message || "Reveal failed");
        }

        setAuthMessage(message || "Password shown on device for 15 seconds.");
    } catch (error) {
        console.error(error);
        setAuthMessage(error.message || "Reveal failed.", "error");
    } finally {
        button.disabled = false;
    }
}

async function changeAdminPassword() {
    const currentPassword = document.getElementById("currentAdminPassword").value;
    const newPassword = document.getElementById("newAdminPassword").value;
    const confirmPassword = document.getElementById("confirmAdminPassword").value;

    if (!newPassword || newPassword !== confirmPassword) {
        document.getElementById("securityStatus").textContent = "New password confirmation does not match.";
        setSyncState("Password mismatch", "error");
        return;
    }

    try {
        setSyncState("Updating password", "pending");
        const response = await postJson("/auth/password", {
            currentPassword,
            newPassword
        });
        const message = await response.text();
        if (!response.ok) {
            throw new Error(message || "Password update failed");
        }

        document.getElementById("currentAdminPassword").value = "";
        document.getElementById("newAdminPassword").value = "";
        document.getElementById("confirmAdminPassword").value = "";
        await refreshAuthStatus();
        document.getElementById("securityStatus").textContent = "Password updated. Sign in again.";
        showAuthOverlay("Password updated. Sign in again.");
        setSyncState("Password updated", "saved");
    } catch (error) {
        console.error(error);
        document.getElementById("securityStatus").textContent = error.message || "Password update failed.";
        setSyncState("Password failed", "error");
    }
}

async function submitLogin(event) {
    event.preventDefault();

    const password = document.getElementById("authPassword").value;
    if (!password) {
        setAuthMessage("Enter the admin password.", "error");
        return;
    }

    try {
        setAuthMessage("Signing in...");
        const response = await postJson("/auth/login", {
            username: document.getElementById("authUsernameLabel").textContent || "admin",
            password
        });
        const message = await response.text();
        if (!response.ok) {
            throw new Error(message || "Sign-in failed");
        }

        hideAuthOverlay();
        await refreshAuthStatus();
        await loadAllState();
    } catch (error) {
        console.error(error);
        showAuthOverlay(error.message || "Sign-in failed.", "error");
    }
}

function switchTab(tabId) {
    const nextTab = ["display", "feeds", "homeassistant", "widgets", "network", "system"].includes(tabId)
        ? tabId
        : "display";

    document.querySelectorAll(".tab").forEach((button) => {
        const active = button.dataset.tab === nextTab;
        button.classList.toggle("active", active);
        button.setAttribute("aria-pressed", active ? "true" : "false");
    });
    document.querySelectorAll(".tab-panel").forEach((panel) => {
        const active = panel.id === `panel-${nextTab}`;
        panel.classList.toggle("active", active);
        panel.hidden = !active;
    });

    if (window.location.hash !== `#${nextTab}`) {
        history.replaceState(null, "", `#${nextTab}`);
    }
}

async function loadFeedStateOnly() {
    if (!authenticated) {
        updateActionButtons();
        return;
    }

    window.clearTimeout(feedLiveTimer);
    feedLiveTimer = 0;

    const response = await request("/feeds.json");
    if (!response.ok) {
        throw new Error("Feed state failed");
    }

    const feedState = await response.json();
    applyFeedState(feedState);
    feedDirty = Boolean(feedState.meta?.hasDraft);
    updateActionButtons();
}

async function loadAllState() {
    if (!authenticated) {
        handleUnauthorized(authCanRevealPassword ? "Use the reveal button or sign in to continue." : "Sign in to continue.");
        return;
    }

    hydrating = true;
    setSyncState("Loading", "pending");
    window.clearTimeout(liveTimer);
    window.clearTimeout(feedLiveTimer);
    liveTimer = 0;
    feedLiveTimer = 0;

    try {
        const [appResponse, dashboardResponse, feedsResponse, versionResponse, spaceResponse] = await Promise.all([
            request("/app.json"),
            request("/dashboard.json"),
            request("/feeds.json"),
            request("/version.json"),
            request("/space.json")
        ]);

        if (!appResponse.ok || !dashboardResponse.ok || !feedsResponse.ok || !versionResponse.ok || !spaceResponse.ok) {
            throw new Error("State request failed");
        }

        const appState = await appResponse.json();
        const dashboardState = await dashboardResponse.json();
        const feedsState = await feedsResponse.json();
        const versionState = await versionResponse.json();
        const spaceState = await spaceResponse.json();

        applyAppState(appState);
        applyDashboardState(dashboardState);
        applyFeedState(feedsState);

        const firmwareVersion = versionState.version || "Unknown";
        const storageText = `${formatBytes(spaceState.free)} free`;

        document.getElementById("firmwareMeta").textContent = firmwareVersion;
        document.getElementById("storageMeta").textContent = storageText;
        document.getElementById("systemVersion").textContent = firmwareVersion;
        document.getElementById("systemStorage").textContent = `${formatBytes(spaceState.total)} / ${formatBytes(spaceState.free)} free`;

        dirty = Boolean(appState.displayDraft || dashboardState.meta?.hasDraft);
        feedDirty = Boolean(feedsState.meta?.hasDraft);

        if (dirty && feedDirty) {
            setSyncState("Display + feeds draft", "live");
        } else if (dirty) {
            setSyncState("Display draft", "live");
        } else if (feedDirty) {
            setSyncState("Feeds draft", "live");
        } else if (appState.networkBusy) {
            setSyncState("Network task", "pending");
        } else {
            setSyncState("Ready");
        }
    } catch (error) {
        console.error(error);
        if (authenticated) {
            setSyncState("Load failed", "error");
        }
    } finally {
        hydrating = false;
        updateActionButtons();
    }
}

document.addEventListener("input", (event) => {
    if (event.target.matches("[data-feed-live]")) {
        if (event.target.id === "homeAssistantBaseUrl") {
            updateHomeAssistantModeUI();
        }
        if (/^haEntityId\d$/.test(event.target.id)) {
            updateHomeAssistantModeUI();
        }
        if (event.target.tagName === "TEXTAREA" || event.target.type === "text" || event.target.type === "password") {
            scheduleFeedPreview(260);
            return;
        }

        scheduleFeedPreview(150);
        return;
    }

    if (event.target.matches("[data-live]")) {
        if (event.target.id === "deviceName") {
            updateDeviceIdentityPreview();
            scheduleLiveSync(260);
            return;
        }

        if (event.target.id === "brightness") {
            updateBrightnessLabel();
            scheduleLiveSync(70);
            return;
        }

        if (event.target.id === "nightBrightness") {
            updateNightBrightnessLabel();
            scheduleLiveSync(70);
            return;
        }

        if (event.target.id === "timezoneCustom") {
            scheduleLiveSync(180);
            return;
        }

        if (event.target.tagName === "TEXTAREA" || event.target.type === "text") {
            scheduleLiveSync(260);
            return;
        }

        scheduleLiveSync(150);
    }
});

document.addEventListener("change", (event) => {
    if (event.target.id === "timezonePreset") {
        handleTimezonePresetChange();
        scheduleLiveSync(100);
        return;
    }

    if (event.target.id === "customThemeEnabled") {
        updateCustomThemeUI();
        scheduleLiveSync(90);
        return;
    }

    if (event.target.id === "nightModeEnabled") {
        updateNightModeUI();
        scheduleLiveSync(90);
        return;
    }

    if (event.target.id === "nightThemeEnabled") {
        updateNightThemeUI();
        scheduleLiveSync(90);
        return;
    }

    if (event.target.id === "nightCustomThemeEnabled") {
        updateNightThemeUI();
        scheduleLiveSync(90);
        return;
    }

    if (event.target.matches("[data-page-toggle]")) {
        ensurePageSelection(event.target.id);
        scheduleLiveSync(90);
        return;
    }

    if (event.target.id === "weatherSource") {
        updateWeatherModeUI();
        scheduleFeedPreview(80);
        return;
    }

    if (/^marketSource\d$/.test(event.target.id)) {
        const index = parseInt(event.target.id.slice(-1), 10);
        updateMarketModeUI(index);
        scheduleFeedPreview(80);
        return;
    }

    if (event.target.id === "homeAssistantEnabled" ||
        event.target.id === "homeAssistantTlsMode" ||
        /^haEnabled\d$/.test(event.target.id)) {
        updateHomeAssistantModeUI();
        scheduleFeedPreview(80);
        return;
    }

    if (event.target.matches("[data-feed-live]")) {
        scheduleFeedPreview(100);
        return;
    }

    if (event.target.matches("[data-live]")) {
        scheduleLiveSync(100);
    }
});

document.querySelectorAll(".tab").forEach((button) => {
    button.addEventListener("click", () => switchTab(button.dataset.tab));
});

window.addEventListener("beforeunload", (event) => {
    if (!dirty && !feedDirty) {
        return;
    }

    event.preventDefault();
    event.returnValue = "";
});

window.addEventListener("hashchange", () => {
    switchTab((window.location.hash || "#display").slice(1));
});

window.addEventListener("load", () => {
    refreshUiThemeButtons();
    populateTimezoneOptions();
    populateOffsetSelect("worldOffset0");
    populateOffsetSelect("worldOffset1");
    populateThemeButtons("themePicker", "themePreset");
    populateThemeButtons("nightThemePicker", "nightThemePreset");
    bindThemeColorInputs("custom");
    bindThemeColorInputs("nightCustom");
    handleTimezonePresetChange();
    updateCustomThemeUI();
    updateNightModeUI();
    updateWeatherModeUI();
    updateMarketModeUI(0);
    updateMarketModeUI(1);
    updateMarketModeUI(2);
    updateHomeAssistantModeUI();
    updateDeviceMeta();
    updateDeviceIdentityPreview();
    switchTab((window.location.hash || "#display").slice(1));
    updateActionButtons();
    document.getElementById("authForm").addEventListener("submit", submitLogin);
    const weatherQueryField = document.getElementById("weatherQuery");
    weatherQueryField.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
            event.preventDefault();
            searchWeatherLocations();
        }
    });
    weatherQueryField.addEventListener("input", () => {
        setValue("weatherLatitude", "");
        setValue("weatherLongitude", "");
        document.getElementById("weatherResults").innerHTML = "";
        document.getElementById("weatherSearchStatus").textContent =
            weatherQueryField.value.trim().length >= 2
                ? "Search or sync to resolve this city."
                : "Enter at least 2 characters.";
    });
    refreshAuthStatus()
        .then(() => {
            if (authenticated) {
                return loadAllState();
            }
            setSyncState("Locked", "error");
            return undefined;
        })
        .catch((error) => {
            console.error(error);
            showAuthOverlay("Unable to reach the device.", "error");
            setSyncState("Auth failed", "error");
        });
});
</script>
</body>
</html>

)rawliteral";

const char ota_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartClock OTA</title>
<script>
(() => {
    try {
        const savedTheme = localStorage.getItem("smartclock-ui-theme");
        const prefersDark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
        document.documentElement.dataset.uiTheme =
            savedTheme === "dark" || savedTheme === "light"
                ? savedTheme
                : (prefersDark ? "dark" : "light");
    } catch (error) {
        document.documentElement.dataset.uiTheme = "light";
    }
})();
</script>
<style>
:root {
    --bg: #eef2f9;
    --bg-top: #f8f9ff;
    --bg-glow: rgba(208, 226, 255, 0.95);
    --surface: #f8f9ff;
    --surface-high: rgba(245, 247, 252, 0.94);
    --surface-strong: #f2f5fb;
    --surface-tint: rgba(255, 255, 255, 0.72);
    --panel-border: rgba(108, 117, 135, 0.18);
    --chip-bg: #e8edf7;
    --line: #c4c9d4;
    --line-strong: #a4acba;
    --ink: #151b26;
    --muted: #566070;
    --accent: #0b57d0;
    --accent-strong: #0b57d0;
    --accent-soft: #d3e3fd;
    --accent-ring: rgba(11, 87, 208, 0.14);
    --accent-border: rgba(11, 87, 208, 0.18);
    --danger: #ba1a1a;
    --danger-soft: #ffdad6;
    --danger-border: rgba(186, 26, 26, 0.18);
    --success: #0f9d58;
    --success-soft: rgba(15, 157, 88, 0.14);
    --success-border: rgba(15, 157, 88, 0.2);
    --text-on-accent: #ffffff;
    --shadow: 0 1px 2px rgba(18, 24, 33, 0.06), 0 18px 36px rgba(18, 24, 33, 0.08);
    --radius: 28px;
    --radius-m: 20px;
    --radius-s: 16px;
    --touch: 48px;
    --mono: "Roboto Mono", "IBM Plex Mono", "SFMono-Regular", "Menlo", monospace;
    --sans: "Roboto Flex", "Roboto", "Segoe UI Variable", "Noto Sans", sans-serif;
}
html {
    color-scheme: light;
}
html[data-ui-theme="dark"] {
    color-scheme: dark;
    --bg: #10131a;
    --bg-top: #181c25;
    --bg-glow: rgba(82, 148, 255, 0.12);
    --surface: #11131a;
    --surface-high: rgba(31, 36, 47, 0.96);
    --surface-strong: #20252f;
    --surface-tint: rgba(22, 25, 34, 0.78);
    --panel-border: rgba(195, 198, 208, 0.12);
    --chip-bg: #252b37;
    --line: #424956;
    --line-strong: #5b6474;
    --ink: #edf1fb;
    --muted: #adb6c7;
    --accent: #a8c7fa;
    --accent-strong: #d3e3fd;
    --accent-soft: #284777;
    --accent-ring: rgba(168, 199, 250, 0.18);
    --accent-border: rgba(168, 199, 250, 0.18);
    --danger: #ffb4ab;
    --danger-soft: rgba(255, 180, 171, 0.14);
    --danger-border: rgba(255, 180, 171, 0.18);
    --success: #7bdba6;
    --success-soft: rgba(123, 219, 166, 0.16);
    --success-border: rgba(123, 219, 166, 0.2);
    --shadow: 0 1px 2px rgba(0, 0, 0, 0.28), 0 20px 44px rgba(0, 0, 0, 0.36);
}
* {
    box-sizing: border-box;
}
html, body {
    margin: 0;
    min-height: 100%;
}
body {
    font-family: var(--sans);
    color: var(--ink);
    background:
        radial-gradient(circle at top left, var(--bg-glow), transparent 30%),
        radial-gradient(circle at bottom right, rgba(255, 255, 255, 0.22), transparent 24%),
        linear-gradient(180deg, var(--bg-top) 0%, var(--bg) 100%);
}
a,
button,
input {
    font: inherit;
}
.app {
    max-width: 1080px;
    margin: 0 auto;
    padding: 24px 20px 40px;
}
.topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 18px;
    margin-bottom: 22px;
    padding: 14px 18px;
    border-radius: 30px;
    border: 1px solid var(--panel-border);
    background: var(--surface-tint);
    box-shadow: var(--shadow);
    backdrop-filter: blur(18px);
}
.brand {
    display: flex;
    align-items: center;
    gap: 16px;
    min-width: 0;
}
.brand-mark {
    width: 44px;
    height: 44px;
    border-radius: 16px;
    display: grid;
    place-items: center;
    background: var(--accent-soft);
    color: var(--accent-strong);
    box-shadow: inset 0 0 0 1px var(--accent-border);
}
.brand-mark::before {
    content: "";
    width: 18px;
    height: 18px;
    border-radius: 8px;
    background: currentColor;
}
.brand h1 {
    margin: 0;
    font-size: 1.28rem;
    font-weight: 600;
    letter-spacing: -0.03em;
}
.brand small {
    display: block;
    color: var(--muted);
    font-size: 0.88rem;
    margin-top: 2px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.top-actions {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: 10px;
    flex-wrap: wrap;
}
.mode-switch {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 4px;
    border-radius: 999px;
    border: 1px solid var(--panel-border);
    background: var(--surface-strong);
}
.mode-button {
    min-height: 36px;
    padding: 0 14px;
    border: 1px solid transparent;
    border-radius: 999px;
    background: transparent;
    color: var(--muted);
    box-shadow: none;
    cursor: pointer;
}
.mode-button.active {
    background: var(--accent-soft);
    color: var(--ink);
}
.mode-button:hover {
    background: var(--chip-bg);
}
.button-link,
button {
    min-height: var(--touch);
    padding: 0 18px;
    border: 1px solid transparent;
    border-radius: 999px;
    background: var(--accent);
    color: var(--text-on-accent);
    cursor: pointer;
    font-weight: 600;
    letter-spacing: 0.01em;
    text-decoration: none;
    display: inline-flex;
    align-items: center;
    justify-content: center;
}
.ghost {
    background: var(--surface-strong);
    color: var(--ink);
    border-color: var(--panel-border);
}
button:disabled,
input:disabled,
.button-link[aria-disabled="true"] {
    opacity: 0.55;
    cursor: not-allowed;
}
.pill {
    display: inline-flex;
    align-items: center;
    min-height: 36px;
    padding: 0 14px;
    border-radius: 999px;
    background: var(--chip-bg);
    color: var(--muted);
    font-weight: 600;
    font-size: 0.88rem;
}
.pill.pending {
    background: var(--accent-soft);
    color: var(--accent-strong);
}
.pill.saved {
    background: var(--success-soft);
    color: var(--success);
}
.pill.error {
    background: var(--danger-soft);
    color: var(--danger);
}
.layout {
    width: 100%;
}
.panel {
    box-sizing: border-box;
    padding: 24px;
    border-radius: 32px;
    border: 1px solid var(--panel-border);
    background: var(--surface-high);
    box-shadow: var(--shadow);
}
.hero-title {
    margin: 0 0 10px;
    font-size: clamp(1.8rem, 4vw, 2.5rem);
    line-height: 1.05;
    letter-spacing: -0.04em;
}
.hero-copy,
.panel-note,
.status-detail,
.field-help {
    color: var(--muted);
    line-height: 1.6;
}
.panel-note,
.status-card {
    margin-top: 18px;
    padding: 16px 18px;
    border-radius: 24px;
    border: 1px solid transparent;
    background: var(--surface-strong);
}
.status-card.saved {
    border-color: var(--success-border);
    background: var(--success-soft);
}
.status-card.error {
    border-color: var(--danger-border);
    background: var(--danger-soft);
}
.status-card.pending {
    border-color: var(--accent-border);
    background: var(--accent-ring);
}
.status-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
}
.status-detail {
    margin: 12px 0 0;
    font-size: 0.92rem;
}
.form-stack {
    display: grid;
    gap: 18px;
}
.field {
    display: grid;
    gap: 10px;
}
.field label {
    font-weight: 600;
}
input[type="file"] {
    width: 100%;
    min-height: 60px;
    padding: 12px 14px;
    border-radius: 20px;
    border: 1px solid var(--panel-border);
    background: var(--surface-strong);
    color: var(--ink);
}
input[type="file"]::file-selector-button {
    margin-right: 14px;
    min-height: 36px;
    padding: 0 16px;
    border: 1px solid transparent;
    border-radius: 999px;
    background: var(--accent-soft);
    color: var(--accent-strong);
    font-weight: 600;
    cursor: pointer;
}
.file-meta {
    min-height: 1.4em;
    color: var(--muted);
    font-size: 0.9rem;
}
.progress-block {
    display: grid;
    gap: 10px;
}
.progress-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    font-weight: 600;
}
.progress-value {
    color: var(--muted);
    font-family: var(--mono);
    font-size: 0.92rem;
    font-variant-numeric: tabular-nums;
}
.progress-track {
    height: 12px;
    border-radius: 999px;
    overflow: hidden;
    background: var(--chip-bg);
}
.progress-fill {
    width: 0;
    height: 100%;
    border-radius: inherit;
    background: linear-gradient(90deg, var(--accent), var(--accent-strong));
    transition: width 180ms ease;
}
.button-row {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
}
.guide-grid {
    display: grid;
    gap: 12px;
    margin-top: 18px;
}
.guide-card {
    padding: 16px 18px;
    border-radius: 22px;
    background: var(--surface-strong);
}
.guide-card strong {
    display: block;
    margin-bottom: 6px;
    font-size: 0.95rem;
}
.guide-card p {
    margin: 0;
    color: var(--muted);
    line-height: 1.55;
    font-size: 0.9rem;
}
@media (max-width: 900px) {
    .topbar {
        flex-direction: column;
        align-items: flex-start;
    }
    .top-actions {
        width: 100%;
        justify-content: flex-start;
    }
}
@media (max-width: 640px) {
    .app {
        padding: 16px 14px 28px;
    }
    .topbar,
    .panel {
        border-radius: 24px;
        padding: 18px;
    }
    .button-row > * {
        flex: 1 1 180px;
    }
}
</style>
</head>
<body>
<div class="app">
    <header class="topbar">
        <div class="brand">
            <div class="brand-mark"></div>
            <div>
                <h1>SmartClock</h1>
                <small id="deviceMeta">Loading firmware metadata…</small>
            </div>
        </div>
        <div class="top-actions">
            <div class="mode-switch" role="group" aria-label="Interface theme">
                <button class="mode-button" type="button" data-ui-theme-button="light" onclick="setUiTheme('light')">Light</button>
                <button class="mode-button" type="button" data-ui-theme-button="dark" onclick="setUiTheme('dark')">Dark</button>
            </div>
            <a class="button-link ghost" href="/">Back to dashboard</a>
        </div>
    </header>

    <main class="layout">
        <section class="panel">
            <h2 class="hero-title">SmartClock OTA</h2>
            <p class="hero-copy">Upload a new firmware image over Wi-Fi.</p>

            <div id="statusCard" class="status-card">
                <div class="status-row">
                    <span id="statusPill" class="pill">Ready</span>
                    <span id="progressText" class="progress-value">0%</span>
                </div>
                <p id="statusDetail" class="status-detail">Choose a firmware image to start the upload.</p>
            </div>

            <form id="otaForm" class="form-stack">
                <div class="field">
                    <label for="firmwareFile">Firmware image</label>
                    <input id="firmwareFile" name="update" type="file" accept=".bin,application/octet-stream">
                    <div id="selectedFile" class="file-meta">No file selected.</div>
                    <div class="field-help">Recommended file: <code>firmware.bin</code></div>
                </div>

                <div class="progress-block">
                    <div class="progress-header">
                        <span>Upload progress</span>
                        <span id="progressLabel">0%</span>
                    </div>
                    <div class="progress-track" aria-hidden="true">
                        <div id="progressFill" class="progress-fill"></div>
                    </div>
                </div>

                <div class="button-row">
                    <button id="uploadButton" type="submit">Upload firmware</button>
                    <a class="button-link ghost" href="/">Cancel</a>
                </div>
            </form>

            <div class="panel-note">
                If the upload fails, keep this page open and try again after the device is fully back online.
            </div>
        </section>
    </main>
</div>

<script>
const uiThemeStorageKey = "smartclock-ui-theme";
let uploadInFlight = false;

function currentUiTheme() {
    return document.documentElement.dataset.uiTheme === "dark" ? "dark" : "light";
}

function refreshUiThemeButtons() {
    const activeTheme = currentUiTheme();
    document.querySelectorAll("[data-ui-theme-button]").forEach((button) => {
        const isActive = button.dataset.uiThemeButton === activeTheme;
        button.classList.toggle("active", isActive);
        button.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
}

function setUiTheme(theme) {
    const nextTheme = theme === "dark" ? "dark" : "light";
    document.documentElement.dataset.uiTheme = nextTheme;
    refreshUiThemeButtons();

    try {
        localStorage.setItem(uiThemeStorageKey, nextTheme);
    } catch (error) {
        console.warn("Theme preference was not persisted.", error);
    }
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes) || bytes <= 0) {
        return "--";
    }
    if (bytes < 1024) {
        return `${bytes} B`;
    }
    if (bytes < 1024 * 1024) {
        return `${(bytes / 1024).toFixed(1)} KB`;
    }
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function setUploadBusy(busy) {
    uploadInFlight = Boolean(busy);
    document.getElementById("firmwareFile").disabled = uploadInFlight;
    document.getElementById("uploadButton").disabled = uploadInFlight;
}

function updateProgress(percent) {
    const safePercent = Math.max(0, Math.min(100, Math.round(percent)));
    document.getElementById("progressFill").style.width = `${safePercent}%`;
    document.getElementById("progressLabel").textContent = `${safePercent}%`;
    document.getElementById("progressText").textContent = `${safePercent}%`;
}

function setStatus(label, tone = "", detail = "") {
    const pill = document.getElementById("statusPill");
    const card = document.getElementById("statusCard");
    pill.textContent = label;
    pill.className = `pill ${tone}`.trim();
    card.className = `status-card ${tone}`.trim();
    document.getElementById("statusDetail").textContent = detail;
}

function updateSelectedFile() {
    const input = document.getElementById("firmwareFile");
    const file = input.files && input.files[0];
    document.getElementById("selectedFile").textContent = file
        ? `${file.name} · ${formatBytes(file.size)}`
        : "No file selected.";
}

async function loadDeviceMeta() {
    const meta = document.getElementById("deviceMeta");

    try {
        const response = await fetch("/version.json");
        if (!response.ok) {
            throw new Error("Version request failed");
        }

        const payload = await response.json();
        if (payload.deviceName && payload.hostName) {
            meta.textContent = `${payload.deviceName} · ${payload.hostName}.local`;
            return;
        }

        meta.textContent = payload.version ? `${payload.version} · OTA ready` : "OTA ready";
    } catch (error) {
        console.error(error);
        meta.textContent = "Firmware metadata unavailable";
    }
}

function uploadFirmware(file) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/update");
        xhr.timeout = 180000;

        xhr.upload.addEventListener("progress", (event) => {
            if (!event.lengthComputable) {
                return;
            }

            updateProgress((event.loaded / event.total) * 100);
        });

        xhr.addEventListener("load", () => {
            const responseText = (xhr.responseText || "").trim();
            if (xhr.status >= 200 && xhr.status < 300) {
                resolve(responseText || "OK");
                return;
            }

            reject(new Error(responseText || `Upload failed (HTTP ${xhr.status})`));
        });

        xhr.addEventListener("error", () => {
            reject(new Error("Network error during firmware upload"));
        });

        xhr.addEventListener("timeout", () => {
            reject(new Error("Upload timed out"));
        });

        const formData = new FormData();
        formData.append("update", file, file.name);
        xhr.send(formData);
    });
}

document.getElementById("firmwareFile").addEventListener("change", updateSelectedFile);

document.getElementById("otaForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    if (uploadInFlight) {
        return;
    }

    const input = document.getElementById("firmwareFile");
    const file = input.files && input.files[0];
    if (!file) {
        setStatus("Pick firmware", "error", "Choose a .bin firmware image first.");
        updateProgress(0);
        return;
    }

    setUploadBusy(true);
    updateProgress(0);
    setStatus("Uploading", "pending", "Firmware upload started. Keep this page open until the device confirms the reboot.");

    try {
        const result = await uploadFirmware(file);
        updateProgress(100);
        setStatus("Rebooting", "saved", result === "OK - Rebooting..."
            ? "Firmware uploaded. The device is restarting now."
            : result);
    } catch (error) {
        console.error(error);
        setStatus("Upload failed", "error", error.message || "Firmware upload failed.");
        setUploadBusy(false);
        return;
    }

    window.setTimeout(() => {
        setUploadBusy(false);
    }, 12000);
});

window.addEventListener("load", () => {
    refreshUiThemeButtons();
    updateSelectedFile();
    loadDeviceMeta();
});
</script>
</body>
</html>

)rawliteral";

#endif
