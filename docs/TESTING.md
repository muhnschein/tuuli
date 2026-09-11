# Device smoke test

Run before every tag, on the Jolla Phone 2026, from the shell, never from the IDE:

    sailjail /usr/bin/harbour-tuuli

Install the RPM that `sfdk check -s harbour` accepted. Each line must pass with no
workaround; a failure blocks the tag.

## Checklist

- [ ] Launches under Sailjail; first run shows the home page in one tab.
- [ ] Address bar: typing a host opens it over https; typing words searches with the selected engine.
- [ ] Back, forward, reload and stop act on the current page; progress shows while loading.
- [ ] A link with `target=_blank` and an in-page navigation both stay in the tab.
- [ ] Tabs page lists open tabs with title and address; tapping switches, the close button closes.
- [ ] New private tab shows "Private tab" in the address bar; its pages do not appear in History.
- [ ] Kill the app (swipe close), relaunch: same tabs, same active tab, private tabs gone.
- [ ] History lists visited pages newest first; search filters; remove and clear work.
- [ ] Bookmark the page from the menu; it appears in Bookmarks; edit and remove work.
- [ ] Share sends the address to another app.
- [ ] Download a file: the transfer UI appears and the file lands in Downloads.
- [ ] Upload a photo in a web form through the platform picker (permissions check).
- [ ] Settings: change the home page and search engine; toggle desktop sites and confirm a site serves its desktop layout.
- [ ] Clear cookies and site data: a logged-in site asks to log in again.
- [ ] Cover shows the current tab's title and favicon; cover action opens a new tab.
- [ ] Rotate the phone: layout stays portrait (landscape is Phase 2).
- [ ] `journalctl -f` shows no QML warnings from `harbour-tuuli` during the above.
