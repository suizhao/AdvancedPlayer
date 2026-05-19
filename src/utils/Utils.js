// 将 URL 格式路径转换为本地文件路径
function urlToLocalPath(urlString) {
    var path = urlString.toString()

    path = decodeURIComponent(path)
    path = path.replace(/\\/g, "/")

    if (path.startsWith("file:///")) {
        path = path.substring(8)
    } else if (path.startsWith("file://")) {
        path = path.substring(7)
    }

    // Windows 盘符处理：/C:/ → C:/
    if (path.length > 2 && path[0] === '/' && path[2] === ':') {
        path = path.substring(1)
    }
    return path
}

// 将本地路径转换为 QUrl 格式
function localPathToUrl(localPath) {
    var normalizedPath = localPath.replace(/\\/g, "/")
    // 确保路径格式正确：file:///C:/path/to/file
    if (normalizedPath.length > 1 && normalizedPath[1] === ':') {
        // Windows 绝对路径：C:/path -> file:///C:/path
        normalizedPath = "file:///" + normalizedPath
    } else if (!normalizedPath.startsWith("file://")) {
        // 相对路径或其他格式
        normalizedPath = "file:///" + normalizedPath
    }
    return Qt.resolvedUrl(normalizedPath)
}

// 格式化文件大小
function formatFileSize(bytes) {
    if (bytes < 1024) {
        return bytes + " B"
    } else if (bytes < 1024 * 1024) {
        return (bytes / 1024).toFixed(1) + " KB"
    } else if (bytes < 1024 * 1024 * 1024) {
        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    } else {
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
    }
}

// 格式化时长
function formatDuration(ms) {
    if (ms <= 0) return "00:00"
    var seconds = Math.floor(ms / 1000)
    return formatTime(seconds)
}

function formatTime(seconds) {
    if (isNaN(seconds) || seconds < 0) {
        return "00:00"
    }

    var totalSeconds = Math.floor(seconds)
    var hours = Math.floor(totalSeconds / 3600)
    var minutes = Math.floor((totalSeconds % 3600) / 60)
    var secs = totalSeconds % 60

    if (hours > 0) {
        return hours + ":" +
               (minutes < 10 ? "0" : "") + minutes + ":" +
               (secs < 10 ? "0" : "") + secs
    }

    return (minutes < 10 ? "0" : "") + minutes + ":" +
           (secs < 10 ? "0" : "") + secs
}

function playbackSpeedToIndex(speed) {
    var s = Number(speed)
    if (!isFinite(s) || s <= 0) {
        return 2
    }

    var eps = 0.01

    if (Math.abs(s - 0.5) <= eps) {
        return 0
    } else if (Math.abs(s - 0.75) <= eps) {
        return 1
    } else if (Math.abs(s - 1.0) <= eps) {
        return 2
    } else if (Math.abs(s - 1.25) <= eps) {
        return 3
    } else if (Math.abs(s - 1.5) <= eps) {
        return 4
    } else if (Math.abs(s - 2.0) <= eps) {
        return 5
    }

    return 2
}