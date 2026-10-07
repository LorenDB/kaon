import QtQuick

import dev.lorendb.kaon

// Gamepad focus for the shell. The left stick and d-pad move the blue ring, A presses it, B goes back.
// Start is the notch button. Bumpers switch Games, Mods, and Settings. The right stick scrolls.
Item {
    id: pads

    property int generation: 0
    readonly property Window host: Window.window
    property Item notch: null
    property Item searchField: null

    function activate() {
        const modal = topModal();
        const item = focusedControl();
        if (modal && (!item || !isAncestor(modal.contentItem, item))) {
            focusDefault();
            return;
        }
        if (!item) {
            focusDefault();
            return;
        }
        // A text field is already being edited. A second press should not also activate something behind it.
        if (isEditableText(item))
            return;
        Gamepad.postKey(Qt.Key_Return);
    }

    function back() {
        const modal = topModal();
        if (modal) {
            modal.close();
            return;
        }

        const current = focusedControl();
        // B cancels a key combination that is being recorded before it leaves the page.
        if (Nav.capturingKeys) {
            if (current && current.cancelCapture)
                current.cancelCapture();
            else
                Nav.capturingKeys = false;
            return;
        }
        const pops = openPopups();
        if (pops.length) {
            for (let i = pops.length - 1; i >= 0; --i) {
                if (current && isAncestor(pops[i].contentItem, current)) {
                    pops[i].close();
                    return;
                }
            }
            pops[pops.length - 1].close();
            return;
        }

        if (current && isText(current)) {
            if (current === searchField && current.text !== "") {
                searchField.clearSearch();
                return;
            }
            host.contentItem.forceActiveFocus();
            return;
        }

        if (Nav.canGoBack) {
            Nav.back();
            Qt.callLater(focusDefault);
            return;
        }

        if (current)
            host.contentItem.forceActiveFocus();
    }

    function canScroll(flick, dx, dy) {
        if (!flick)
            return false;
        if (dy > 0)
            return flick.contentY < flick.originY + flick.contentHeight - flick.height - 1;
        if (dy < 0)
            return flick.contentY > flick.originY + 1;
        if (dx > 0)
            return flick.contentX < flick.originX + flick.contentWidth - flick.width - 1;
        if (dx < 0)
            return flick.contentX > flick.originX + 1;
        return false;
    }

    function collect(root, out) {
        const stack = [root];
        while (stack.length) {
            const item = stack.pop();
            if (!item || item.visible === false || item.opacity === 0 || item.enabled === false)
                continue;
            if (item.activeFocusOnTab && item.width > 1 && item.height > 1)
                out.push(item);
            const kids = item.children;
            if (!kids)
                continue;
            for (let i = kids.length - 1; i >= 0; --i)
                stack.push(kids[i]);
        }
    }

    function ensureVisible(item) {
        let parentItem = item.parent;
        while (parentItem) {
            if (parentItem.flickableDirection !== undefined && parentItem.contentItem) {
                const pos = item.mapToItem(parentItem.contentItem, 0, 0);
                const top = pos.y;
                const bottom = top + item.height;
                const flickBottom = parentItem.mapToItem(host.contentItem, 0, parentItem.height).y;
                const marginBottom = flickBottom > host.height - Theme.bottomStrap ? Theme.notchHeight + 52 : 12;
                // A page with its way back pinned over the top says how much that covers
                const marginTop = (parentItem.pinnedTop ?? 0) + 12;
                let y = parentItem.contentY;
                if (top < y + marginTop)
                    y = top - marginTop;
                else if (bottom > y + parentItem.height - marginBottom)
                    y = bottom - parentItem.height + marginBottom;
                const maxY = parentItem.originY + Math.max(0, parentItem.contentHeight - parentItem.height);
                parentItem.contentY = Math.max(parentItem.originY, Math.min(y, maxY));
            }
            parentItem = parentItem.parent;
        }
    }

    function flickableOf(item) {
        let parentItem = item;
        while (parentItem) {
            if (parentItem.flickableDirection !== undefined && parentItem.originY !== undefined)
                return parentItem;
            parentItem = parentItem.parent;
        }
        return null;
    }

    function focusDefault() {
        const modal = topModal();
        const items = [];
        collect(modal ? modal.contentItem : host.contentItem, items);
        if (!items.length)
            return;

        if (modal) {
            for (let i = items.length - 1; i >= 0; --i) {
                if (items[i].solid) {
                    items[i].forceActiveFocus();
                    return;
                }
            }
        }

        let pool = items;
        if (!modal) {
            const visor = [];
            for (let i = 0; i < items.length; ++i) {
                const rect = rectOf(items[i]);
                if (rect.top >= Theme.topStrap && rect.top < host.height - Theme.bottomStrap)
                    visor.push(items[i]);
            }
            if (visor.length)
                pool = visor;
        }

        pool.sort((a, b) => {
            const ra = rectOf(a);
            const rb = rectOf(b);
            if (Math.abs(ra.top - rb.top) > 20)
                return ra.top - rb.top;
            return ra.left - rb.left;
        });
        pool[0].forceActiveFocus();
        ensureVisible(pool[0]);
    }

    function focusSearch() {
        if (topModal() || !searchField)
            return;
        Nav.view = "library";
        Qt.callLater(() => searchField.forceActiveFocus());
    }

    function focusedControl() {
        const item = host ? host.activeFocusItem : null;
        if (!item || !item.activeFocusOnTab || item.enabled === false || item.width < 2 || item.height < 2)
            return null;
        let parentItem = item;
        while (parentItem) {
            if (parentItem.visible === false || parentItem.opacity === 0 || parentItem.enabled === false)
                return null;
            parentItem = parentItem.parent;
        }
        return item;
    }

    function isAncestor(ancestor, item) {
        let parentItem = item;
        while (parentItem) {
            if (parentItem === ancestor)
                return true;
            parentItem = parentItem.parent;
        }
        return false;
    }

    function isEditableText(item) {
        return isText(item) && !item.readOnly;
    }

    function isText(item) {
        return item instanceof TextInput || item instanceof TextEdit;
    }

    function mainFlickable() {
        let best = null;
        let bestArea = 0;
        const stack = [host.contentItem];
        while (stack.length) {
            const item = stack.pop();
            if (!item || item.visible === false)
                continue;
            if (item.flickableDirection !== undefined && item.contentHeight > item.height + 2 && item.width > 200) {
                const area = item.width * item.height;
                if (area > bestArea) {
                    best = item;
                    bestArea = area;
                }
            }
            const kids = item.children;
            if (!kids)
                continue;
            for (let i = 0; i < kids.length; ++i)
                stack.push(kids[i]);
        }
        return best;
    }

    function move(dx, dy) {
        if (!dx && !dy)
            return;
        generation++;
        const current = focusedControl();
        if (current && dy === 0 && isText(current) && moveCaret(current, dx))
            return;
        if (!current) {
            focusDefault();
            return;
        }
        moveFrom(dx, dy, rectOf(current), true);
    }

    function moveCaret(item, dx) {
        const pos = item.cursorPosition;
        if (dx < 0 && pos > 0) {
            item.cursorPosition = pos - 1;
            return true;
        }
        if (dx > 0 && pos < item.length) {
            item.cursorPosition = pos + 1;
            return true;
        }
        return false;
    }

    // allowNudge scrolls a list when the next row has not been built yet, then tries once more.
    function moveFrom(dx, dy, origin, allowNudge) {
        const root = scopeItem();
        if (!root)
            return;
        const items = [];
        collect(root, items);

        const current = focusedControl();
        let best = null;
        let bestScore = Infinity;
        for (let i = 0; i < items.length; ++i) {
            const item = items[i];
            if (item === current)
                continue;
            const rect = rectOf(item);
            if (Math.abs(rect.cx - origin.cx) < 4 && Math.abs(rect.cy - origin.cy) < 4)
                continue;
            const primary = dx !== 0 ? (rect.cx - origin.cx) * dx : (rect.cy - origin.cy) * dy;
            if (primary < 6)
                continue;
            const secondary = dx !== 0 ? Math.abs(rect.cy - origin.cy) : Math.abs(rect.cx - origin.cx);
            const overlap = dx !== 0 ? Math.min(origin.bottom, rect.bottom) - Math.max(origin.top, rect.top) : Math.min(
                                           origin.right, rect.right) - Math.max(origin.left, rect.left);
            const score = primary + secondary * (overlap > 0 ? 0.35 : 1.75);
            if (score < bestScore) {
                bestScore = score;
                best = item;
            }
        }

        const list = current ? flickableOf(current) : null;
        const leavingList = best && list && list.contentHeight > list.height + 2 && !isAncestor(list, best);
        if (allowNudge && (!best || leavingList) && canScroll(list, dx, dy)) {
            scrollFlick(list, dx, dy);
            scheduleRetry(dx, dy, origin);
            return;
        }
        if (!best && allowNudge) {
            const page = mainFlickable();
            if (page && page !== list && canScroll(page, dx, dy)) {
                scrollFlick(page, dx, dy);
                scheduleRetry(dx, dy, origin);
            }
            return;
        }
        if (!best)
            return;
        best.forceActiveFocus();
        ensureVisible(best);
    }

    function moveFromSaved(dx, dy, left, top, width, height, stamp) {
        if (stamp !== generation)
            return;
        moveFrom(dx, dy, {
                     "left": left,
                     "top": top,
                     "width": width,
                     "height": height,
                     "cx": left + width / 2,
                     "cy": top + height / 2,
                     "right": left + width,
                     "bottom": top + height
                 }, false);
    }

    function openPopups() {
        const found = [];
        const seen = new Map();
        const stack = [host];
        let guard = 0;
        while (stack.length && guard < 8000) {
            const node = stack.pop();
            guard++;
            if (!node || seen.has(node))
                continue;
            seen.set(node, true);
            if (node.opened === true && node.closePolicy !== undefined && node.contentItem)
                found.push(node);
            pushNodes(stack, node.data);
            pushNodes(stack, node.children);
        }
        return found;
    }

    function primary() {
        if (topModal()) {
            activate();
            return;
        }
        if (notch && notch.enabled !== false)
            notch.clicked();
    }

    function pushNodes(stack, list) {
        if (!list || list.length === undefined)
            return;
        for (let i = 0; i < list.length; ++i)
            stack.push(list[i]);
    }

    function rectOf(item) {
        const pos = item.mapToItem(host.contentItem, 0, 0);
        return {
            "left": pos.x,
            "top": pos.y,
            "width": item.width,
            "height": item.height,
            "cx": pos.x + item.width / 2,
            "cy": pos.y + item.height / 2,
            "right": pos.x + item.width,
            "bottom": pos.y + item.height
        };
    }

    function scheduleRetry(dx, dy, origin) {
        retry.dx = dx;
        retry.dy = dy;
        retry.left = origin.left;
        retry.top = origin.top;
        retry.itemWidth = origin.width;
        retry.itemHeight = origin.height;
        retry.stamp = generation;
        retry.restart();
    }

    function scopeItem() {
        const modal = topModal();
        if (modal)
            return modal.contentItem;
        const current = focusedControl();
        const pops = openPopups();
        for (let i = pops.length - 1; i >= 0; --i) {
            if (current && isAncestor(pops[i].contentItem, current))
                return pops[i].contentItem;
        }
        return host.contentItem;
    }

    function scrollBy(dx, dy) {
        let flick = flickableOf(focusedControl());
        if (!flick || flick.contentHeight <= flick.height + 2)
            flick = mainFlickable();
        if (!flick)
            return;
        const maxY = flick.originY + Math.max(0, flick.contentHeight - flick.height);
        flick.contentY = Math.max(flick.originY, Math.min(flick.contentY + dy * 14, maxY));
        if (flick.contentWidth > flick.width + 2) {
            const maxX = flick.originX + Math.max(0, flick.contentWidth - flick.width);
            flick.contentX = Math.max(flick.originX, Math.min(flick.contentX + dx * 14, maxX));
        }
    }

    function scrollFlick(flick, dx, dy) {
        if (dy !== 0) {
            const delta = dy * Math.max(80, flick.height * 0.35);
            const maxY = flick.originY + Math.max(0, flick.contentHeight - flick.height);
            flick.contentY = Math.max(flick.originY, Math.min(flick.contentY + delta, maxY));
        }
        if (dx !== 0 && flick.contentWidth > flick.width + 2) {
            const delta = dx * Math.max(80, flick.width * 0.35);
            const maxX = flick.originX + Math.max(0, flick.contentWidth - flick.width);
            flick.contentX = Math.max(flick.originX, Math.min(flick.contentX + delta, maxX));
        }
    }

    // Page Up, Page Down, Home and End for whatever page is showing
    function scrollPage(direction) {
        const flick = mainFlickable();
        if (flick)
            scrollTo(flick, flick.contentY + direction * Math.max(120, flick.height - Theme.notchHeight - 80));
    }

    function scrollTo(flick, y) {
        const maxY = flick.originY + Math.max(0, flick.contentHeight - flick.height);
        glide.stop();
        glide.target = flick;
        glide.to = Math.max(flick.originY, Math.min(y, maxY));
        glide.start();
    }

    function scrollToEnd(direction) {
        const flick = mainFlickable();
        if (!flick)
            return;
        // A list only guesses how long it is until its rows exist, so it has to find its own ends
        if (flick.positionViewAtEnd !== undefined) {
            glide.stop();
            if (direction < 0)
                flick.positionViewAtBeginning();
            else
                flick.positionViewAtEnd();
        } else
            scrollTo(flick, direction < 0 ? flick.originY : flick.originY + flick.contentHeight);
    }

    function switchTab(direction) {
        if (topModal() || openPopups().length)
            return;
        const tabs = ["library", "mods", "settings"];
        let view = Nav.view === "game" || Nav.view === "modConfig" ? "library" : Nav.view === "addGame" ? Nav.addGameFrom :
                                                                                                          Nav.view;
        let index = tabs.indexOf(view);
        if (index < 0)
            index = 0;
        Nav.view = tabs[(index + direction + tabs.length) % tabs.length];
        Qt.callLater(focusDefault);
    }

    function topModal() {
        const pops = openPopups();
        let modal = null;
        for (let i = 0; i < pops.length; ++i) {
            if (pops[i].modal && pops[i].contentItem)
                modal = pops[i];
        }
        return modal;
    }

    NumberAnimation {
        id: glide

        duration: 160
        easing.type: Easing.OutCubic
        property: "contentY"

        // A list only guesses its length until its rows exist. Where the guess was long, this settles on the real end.
        onFinished: if (target)
                        target.returnToBounds()
    }

    Timer {
        id: retry

        property int dx
        property int dy
        property real itemHeight
        property real itemWidth
        property real left
        property int stamp
        property real top

        interval: 60

        onTriggered: pads.moveFromSaved(dx, dy, left, top, itemWidth, itemHeight, stamp)
    }

    Connections {
        function onActivate() {
            pads.activate();
        }

        function onBack() {
            pads.back();
        }

        function onNavigate(dx, dy) {
            pads.move(dx, dy);
        }

        function onPrimary() {
            pads.primary();
        }

        function onScroll(dx, dy) {
            pads.scrollBy(dx, dy);
        }

        function onSearch() {
            pads.focusSearch();
        }

        function onTab(direction) {
            pads.switchTab(direction);
        }

        enabled: pads.host && pads.host.active
        target: Gamepad
    }
}
