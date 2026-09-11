// Stub page stack: synchronous, no transitions, pages parented to the stack.
import QtQuick 2.6

Item {
    id: stack

    property var pages: []
    property Item currentPage: null
    property int depth: 0
    property bool busy: false

    function instantiate(page, properties) {
        var component = (typeof page === "object" && page.createObject) ? page
                                                                        : Qt.createComponent(page)
        if (component.status === Component.Error) {
            console.error(component.errorString())
            return null
        }
        return component.createObject(stack, properties || {})
    }

    function refresh() {
        var list = pages
        for (var i = 0; i < list.length; ++i) {
            list[i].status = (i === list.length - 1) ? 2 : 0
        }
        currentPage = list.length > 0 ? list[list.length - 1] : null
        depth = list.length
    }

    function push(page, properties, operation) {
        var item = instantiate(page, properties)
        if (!item) {
            return null
        }
        var list = pages
        list.push(item)
        pages = list
        refresh()
        return item
    }

    function pop(page, operation) {
        var list = pages
        if (list.length <= 1) {
            return null
        }
        var target = page ? page : list[list.length - 2]
        while (list.length > 1 && list[list.length - 1] !== target) {
            list.pop().destroy()
        }
        pages = list
        refresh()
        return currentPage
    }

    function replace(page, properties, operation) {
        var list = pages
        if (list.length > 0) {
            list.pop().destroy()
        }
        pages = list
        return push(page, properties, operation)
    }

    function find(predicate) {
        var list = pages
        for (var i = list.length - 1; i >= 0; --i) {
            if (predicate(list[i])) {
                return list[i]
            }
        }
        return null
    }

    function previousPage(page) {
        var list = pages
        var index = list.indexOf(page ? page : currentPage)
        return index > 0 ? list[index - 1] : null
    }

    function navigateBack(operation) {
        return pop()
    }

    function completeAnimation() {
    }
}
