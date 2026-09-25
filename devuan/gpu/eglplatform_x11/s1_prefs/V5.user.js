// S1 V5 — ekstra-variant (25. sep 2026, lagt til under kørslen): WebRender på
// GPU'en MEN uden GPU-proces, dvs. kompositoren i samme proces som browseren.
// Motivation: V3 viste at GPU-processen bruger hybris-EGL og dør med
// DeviceReset DRIVER_ERROR (WR_POST_UPDATE) — hvis WebRender kan køre
// in-process, undgår vi både det døde barn OG readback'en af WebGL-canvas.
user_pref("gfx.x11-egl.force-enabled", true);
user_pref("webgl.force-enabled", true);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.page", 0);
user_pref("startup.homepage_welcome_url", "about:blank");
user_pref("browser.tabs.warnOnClose", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("app.update.enabled", false);
user_pref("toolkit.telemetry.enabled", false);
// --- S1 V5 (WebRender/GPU uden GPU-proces)
user_pref("gfx.webrender.enabled", true);
user_pref("gfx.webrender.force-disabled", false);
user_pref("gfx.webrender.software", false);
user_pref("layers.acceleration.disabled", false);
user_pref("layers.gpu-process.enabled", false);
user_pref("layers.omtp.enabled", false);
