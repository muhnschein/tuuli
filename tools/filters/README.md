# Cosmetic filter lists

Drop EasyList-format lists (`*.txt`) into

    ~/.local/share/org.tuuli/browser/filters/

and toggle "Cosmetic filtering" in Settings.  Only element-hiding rules
(`##`, `domain##`, `#@#`) are used; network rules are ignored because Servo
has no request-interception API (spec 9.3).  This is not ad blocking and the
UI never calls it that.

No list ships with the package.
