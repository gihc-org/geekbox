/* Content-script: injicerer shim'en i SIDENS verden (content scripts kører
 * i en isoleret verden og kan ikke patche sidens HTMLCanvasElement ellers). */
var shim = '(' + function () {
    var realGetContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function () {
        var args = Array.prototype.slice.call(arguments);
        if (args[1] && typeof args[1] === 'object') {
            var a = {};
            for (var k in args[1]) a[k] = args[1][k];
            if (a.alpha === false) a.alpha = true;
            if (a.premultipliedAlpha === false) a.premultipliedAlpha = true;
            args[1] = a;
        }
        var r = realGetContext.apply(this, args);
        try {
            if (r && r.getExtension) {
                var ge = r.getExtension.bind(r);
                r.getExtension = function (n) {
                    if (String(n).toUpperCase() === 'WEBGL_LOSE_CONTEXT') {
                        return { loseContext: function () {}, restoreContext: function () {} };
                    }
                    return ge(n);
                };
            }
        } catch (e) {}
        return r;
    };
} + ')();';
var el = document.createElement('script');
el.textContent = shim;
(document.documentElement || document).appendChild(el);
