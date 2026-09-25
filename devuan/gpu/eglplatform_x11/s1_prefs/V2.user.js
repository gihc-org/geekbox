// S1 V2 — WebRender med SWGL (software-rasterizer).
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
user_pref("layers.gpu-process.enabled", true);
// --- S1 V2 (SWGL)
user_pref("gfx.webrender.enabled", true);
user_pref("gfx.webrender.force-disabled", false);
user_pref("gfx.webrender.software", true);
user_pref("layers.acceleration.disabled", true);
