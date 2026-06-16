// VCU Dashboard V10 — PNG-first architecture.
// Section 1: Background.png + Car.png  |  Section 2: SVG gauges + battery  |  Section 3: Text + map
// Qt-drawn carCanvas, bloom layers, and warningRow removed.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Shapes 1.15

Window {
    id: root
    width: 800; height: 480
    visible: true
    visibility: Window.FullScreen
    flags: Qt.FramelessWindowHint | Qt.Window
    color: "#000000"

    readonly property real uniformScale: Math.min(root.width / 800.0, root.height / 480.0)

    FontLoader { id: zeroHour; source: "qrc:/assets/fonts/ZeroHour.otf" }

    // Drive-mode theme — NORMAL=#FFFFFF  ECO=#B8FF01  SPORT=#FF3300
    property color themeColor: {
        if (vehicleModel.mode === "SPORT") return "#FF3300"
        if (vehicleModel.mode === "ECO")   return "#B8FF01"
        return "#FFFFFF"
    }
    // Battery SoC color — independent of themeColor
    property color batteryColor: {
        if (vehicleModel.batterySoC >= 60) return "#FFFFFF"
        if (vehicleModel.batterySoC >= 20) return "#FFCC00"
        return "#FF3333"
    }
    Behavior on themeColor   { ColorAnimation { duration: 300 } }
    Behavior on batteryColor { ColorAnimation { duration: 300 } }

    Item {
        id: dashboardScene
        width: 800; height: 480
        anchors.centerIn: parent
        transform: Scale {
            xScale: root.uniformScale; yScale: root.uniformScale
            origin.x: 400; origin.y: 240
        }

        // ── SECTION 1: PNG layers (background then car) ───────────────────────

        Image {
            id: bgImg
            anchors.fill: parent
            source: "qrc:/assets/Background.png"
            fillMode: Image.Stretch
            smooth: true
            z: 0
        }

        Image {
            id: carImg
            x: 352; y: 258; width: 107; height: 75
            source: "qrc:/assets/Car.png"
            fillMode: Image.Stretch
            smooth: true
            z: 1
        }

        // ── SECTION 2: SVG gauge + battery ───────────────────────────────────

        Item {
            id: gaugeGroup
            x: 30; y: 110
            width: 260; height: 270
            scale: 1.0
            transformOrigin: Item.TopLeft
            z: 2

            Item {
                id: gaugeItem
                width: 260; height: 260
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }

                property real speedSweep: (Math.min(vehicleModel.speed, 300) / 300.0) * 194.0

                // Speed track SVG
                Image {
                    id: speedTrackImg
                    anchors.fill: parent
                    source: "qrc:/assets/Speed_Track.svg"
                    fillMode: Image.Stretch
                    smooth: true
                }

                // Speed fill — Speed_Fill.svg clipped to sector, colorized with theme gradient
                Canvas {
                    id: speedFillCanvas
                    anchors.fill: parent
                    property real speedSweep: gaugeItem.speedSweep
                    property color fillColor: root.themeColor
                    onSpeedSweepChanged: requestPaint()
                    onFillColorChanged: requestPaint()
                    Component.onCompleted: loadImage("qrc:/assets/Speed_Fill.svg")
                    onImageLoaded: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if (speedSweep <= 0 || !isImageLoaded("qrc:/assets/Speed_Fill.svg")) return
                        var cx = width * 0.5, cy = height * 0.5
                        var startRad = 168 * Math.PI / 180
                        var endRad   = (171 + speedSweep + 6.0) * Math.PI / 180
                        ctx.save()
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.arc(cx, cy, 200, startRad, endRad, false)
                        ctx.closePath()
                        ctx.clip()
                        ctx.drawImage("qrc:/assets/Speed_Fill.svg", 0, 0, width, height)
                        var r = Math.round(fillColor.r * 255)
                        var g = Math.round(fillColor.g * 255)
                        var b = Math.round(fillColor.b * 255)
                        var lr = Math.min(255, Math.round(r + (255 - r) * 0.55))
                        var lg = Math.min(255, Math.round(g + (255 - g) * 0.55))
                        var lb = Math.min(255, Math.round(b + (255 - b) * 0.55))
                        var grad = ctx.createLinearGradient(cx - 100, cy + 100, cx + 100, cy - 100)
                        grad.addColorStop(0, "rgb(" + lr + "," + lg + "," + lb + ")")
                        grad.addColorStop(1, "rgb(" + r  + "," + g  + "," + b  + ")")
                        ctx.globalCompositeOperation = "source-atop"
                        ctx.fillStyle = grad
                        ctx.fillRect(0, 0, width, height)
                        ctx.restore()
                    }
                }

                // ── SECTION 3 (gauge readouts) ─────────────────────────────────
                // Speed value, mode label, energy %, and Battery_Track.svg bar
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 25
                    spacing: 2
                    z: 5

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: vehicleModel.speed
                        color: root.themeColor
                        font { pixelSize: 30; bold: true; family: zeroHour.name }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "km/h"; color: "#BBBBBB"
                        font { pixelSize: 13; family: zeroHour.name; letterSpacing: 3 }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: vehicleModel.mode
                        color: root.themeColor; opacity: 0.75
                        font { pixelSize: 12; family: zeroHour.name; letterSpacing: 4 }
                    }
                    Item { width: 1; height: 4 }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "ENERGY"; color: "#BBBBBB"
                            font { pixelSize: 9; family: zeroHour.name; letterSpacing: 3 }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: vehicleModel.batterySoC + "%"
                            color: root.batteryColor
                            font { pixelSize: 10; family: zeroHour.name }
                            Behavior on color { ColorAnimation { duration: 300 } }
                        }
                    }
                    // Battery_Track.svg bar + parallelogram fill
                    Item {
                        width: 160; height: 20
                        anchors.horizontalCenter: parent.horizontalCenter
                        Image {
                            id: batteryTrackImg
                            anchors.fill: parent
                            source: "qrc:/assets/Battery_Track.svg"
                            fillMode: Image.Stretch
                            smooth: true
                        }
                        Canvas {
                            id: batteryFillCanvas
                            width: 160; height: 20
                            property real socFraction: vehicleModel.batterySoC / 100.0
                            property color fillColor: root.batteryColor
                            onSocFractionChanged: requestPaint()
                            onFillColorChanged: requestPaint()
                            Component.onCompleted: loadImage("qrc:/assets/Battery_Fill.svg")
                            onImageLoaded: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                if (socFraction <= 0 || !isImageLoaded("qrc:/assets/Battery_Fill.svg")) return
                                var fillW = width * socFraction
                                ctx.save()
                                ctx.beginPath()
                                ctx.rect(0, 0, fillW, height)
                                ctx.clip()
                                ctx.drawImage("qrc:/assets/Battery_Fill.svg", 0, 0, width, height)
                                var r = Math.round(fillColor.r * 255)
                                var g = Math.round(fillColor.g * 255)
                                var b = Math.round(fillColor.b * 255)
                                ctx.globalCompositeOperation = "source-atop"
                                ctx.fillStyle = "rgb(" + r + "," + g + "," + b + ")"
                                ctx.fillRect(0, 0, width, height)
                                ctx.restore()
                            }
                        }
                    }
                } // center Column
            } // gaugeItem
        } // gaugeGroup

        // ── SECTION 3: Text overlays + map ───────────────────────────────────

        // Clock + date — top-left header pocket
        Item {
            id: timeZone
            x: 45; y: 60
            width: 155; height: 34
            z: 3
            Column {
                anchors.centerIn: parent
                spacing: 2
                Text {
                    id: clockText
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.themeColor
                    font { pixelSize: 11; family: zeroHour.name }
                    property var _d: new Date()
                    text: {
                        var d = vehicleModel.systemTimestamp > 0
                            ? new Date(vehicleModel.systemTimestamp * 1000) : _d
                        return Qt.formatTime(d, "hh:mm AP")
                    }
                    Timer { interval: 1000; running: true; repeat: true; onTriggered: clockText._d = new Date() }
                }
                Text {
                    id: dateText
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: "#BBBBBB"
                    font { pixelSize: 8; family: zeroHour.name; letterSpacing: 1 }
                    property var _d: new Date()
                    text: {
                        var d = vehicleModel.systemTimestamp > 0
                            ? new Date(vehicleModel.systemTimestamp * 1000) : _d
                        return Qt.formatDate(d, "dd MMM yyyy")
                    }
                    Timer { interval: 60000; running: true; repeat: true; onTriggered: dateText._d = new Date() }
                }
            }
        }

        // Temperature — upper-right, same row as timeZone
        Item {
            id: tempZone
            x: 620; y: 60
            width: 118; height: 38
            z: 3
            Column {
                anchors.centerIn: parent
                spacing: 1
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: vehicleModel.bmp180Ok ? vehicleModel.temperature + "°C" : "---"
                    color: (vehicleModel.bmp180Ok && vehicleModel.temperature > 70) ? "#FF4444" : root.themeColor
                    font { pixelSize: 20; family: zeroHour.name }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "TEMP"; color: "#BBBBBB"
                    font { pixelSize: 9; family: zeroHour.name; letterSpacing: 3 }
                }
            }
        }


        // ── Status Dashboard ──────────────────────────────────────────────
        Rectangle {
            id: statusDashboard
            x: 328; y: 130
            width: 145; height: 80
            color: "#B0000000"
            radius: 4
            z: 4
            clip: true

            function dotColor(nodeAlive, moduleOk) {
                if (!nodeAlive) return "#FF3300"
                return moduleOk ? "#B8FF01" : "#FFCC00"
            }
            function n1Text() {
                if (!vehicleModel.node1Heartbeat) return "Node 1  Disconnected"
                return "Node 1  OK"
            }
            function n2Text() {
                if (!vehicleModel.node2Heartbeat) return "Node 2  Disconnected"
                var e = []
                if (!vehicleModel.socOk)  e.push("Battery")
                if (!vehicleModel.gearOk) e.push("Gear")
                if (!vehicleModel.modeOk) e.push("D-Mode")
                return e.length > 0 ? e.join(", ") + " ERROR" : "Node 2  OK"
            }
            function n3Text() {
                if (!vehicleModel.node3Heartbeat) return "Node 3  Disconnected"
                var e = []
                if (!vehicleModel.bmp180Ok) e.push("Temp")
                if (!vehicleModel.ds3231Ok) e.push("RTC")
                return e.length > 0 ? e.join(", ") + " ERROR" : "Node 3  OK"
            }

            Column {
                anchors { fill: parent; margins: 5 }
                spacing: 3

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "STATUS"
                    color: "#BBBBBB"
                    font { pixelSize: 8; family: zeroHour.name; letterSpacing: 2 }
                }

                Item {
                    width: parent.width; height: 12
                    Rectangle {
                        id: n1Dot
                        width: 7; height: 7; radius: 3
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        color: statusDashboard.dotColor(vehicleModel.node1Heartbeat, true)
                    }
                    Text {
                        anchors { left: n1Dot.right; leftMargin: 3; right: parent.right; verticalCenter: parent.verticalCenter }
                        text: statusDashboard.n1Text()
                        color: vehicleModel.node1Heartbeat ? "#BBBBBB" : "#FF5555"
                        font { pixelSize: 9; family: zeroHour.name }
                        elide: Text.ElideRight
                    }
                }

                Item {
                    width: parent.width; height: 12
                    Rectangle {
                        id: n2Dot
                        width: 7; height: 7; radius: 3
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        color: statusDashboard.dotColor(vehicleModel.node2Heartbeat,
                                                        vehicleModel.socOk && vehicleModel.gearOk && vehicleModel.modeOk)
                    }
                    Text {
                        anchors { left: n2Dot.right; leftMargin: 3; right: parent.right; verticalCenter: parent.verticalCenter }
                        text: statusDashboard.n2Text()
                        color: vehicleModel.node2Heartbeat ? "#BBBBBB" : "#FF5555"
                        font { pixelSize: 9; family: zeroHour.name }
                        elide: Text.ElideRight
                    }
                }

                Item {
                    width: parent.width; height: 12
                    Rectangle {
                        id: n3Dot
                        width: 7; height: 7; radius: 3
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        color: statusDashboard.dotColor(vehicleModel.node3Heartbeat,
                                                        vehicleModel.bmp180Ok && vehicleModel.ds3231Ok && vehicleModel.signalOk)
                    }
                    Text {
                        anchors { left: n3Dot.right; leftMargin: 3; right: parent.right; verticalCenter: parent.verticalCenter }
                        text: statusDashboard.n3Text()
                        color: vehicleModel.node3Heartbeat ? "#BBBBBB" : "#FF5555"
                        font { pixelSize: 9; family: zeroHour.name }
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // ── RPM gauge — right pod, mirrors speed gauge geometry ──────────────

        Item {
            id: rpmGaugeGroup
            x: 540; y: 110
            width: 260; height: 270
            scale: 1.0
            transformOrigin: Item.TopLeft
            z: 2

            Item {
                id: rpmGaugeItem
                width: 260; height: 260
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }

                property real rpmSweep: (Math.min(vehicleModel.rpm, 10000) / 10000.0) * 194.0

                // RPM track SVG — full arc background
                Image {
                    id: rpmTrackImg
                    anchors.fill: parent
                    source: "qrc:/assets/RPM_Track.svg"
                    fillMode: Image.Stretch
                    smooth: true
                }

                // RPM fill — RPM_Fill.svg clipped to sector, colorized with theme gradient
                Canvas {
                    id: rpmFillCanvas
                    anchors.fill: parent
                    property real rpmSweep: rpmGaugeItem.rpmSweep
                    property color fillColor: root.themeColor
                    onRpmSweepChanged: requestPaint()
                    onFillColorChanged: requestPaint()
                    Component.onCompleted: loadImage("qrc:/assets/RPM_Fill.svg")
                    onImageLoaded: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if (rpmSweep <= 0 || !isImageLoaded("qrc:/assets/RPM_Fill.svg")) return
                        var cx = width * 0.5, cy = height * 0.5
                        var startRad = 168 * Math.PI / 180
                        var endRad   = (171 + rpmSweep + 6.0) * Math.PI / 180
                        ctx.save()
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.arc(cx, cy, 200, startRad, endRad, false)
                        ctx.closePath()
                        ctx.clip()
                        ctx.drawImage("qrc:/assets/RPM_Fill.svg", 0, 0, width, height)
                        var r = Math.round(fillColor.r * 255)
                        var g = Math.round(fillColor.g * 255)
                        var b = Math.round(fillColor.b * 255)
                        var lr = Math.min(255, Math.round(r + (255 - r) * 0.55))
                        var lg = Math.min(255, Math.round(g + (255 - g) * 0.55))
                        var lb = Math.min(255, Math.round(b + (255 - b) * 0.55))
                        var grad = ctx.createLinearGradient(cx - 100, cy + 100, cx + 100, cy - 100)
                        grad.addColorStop(0, "rgb(" + lr + "," + lg + "," + lb + ")")
                        grad.addColorStop(1, "rgb(" + r  + "," + g  + "," + b  + ")")
                        ctx.globalCompositeOperation = "source-atop"
                        ctx.fillStyle = grad
                        ctx.fillRect(0, 0, width, height)
                        ctx.restore()
                    }
                }

                // Center readout — RPM value with krpm transition at 1000
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 30
                    spacing: 2
                    z: 5

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        // kRPM format above 1000 keeps the display to ≤4 chars at all times:
                        //   0–999   → "750"          (exact, 3 chars)
                        //   1000–9999 → "9.9k"       (4 chars)
                        //   10000   → "10k"          (3 chars — prevents overlap with gauge arc)
                        text: vehicleModel.rpm >= 10000 ? "10k"
                            : vehicleModel.rpm >= 1000  ? (vehicleModel.rpm / 1000).toFixed(1) + "k"
                            : vehicleModel.rpm.toString()
                        color: root.themeColor
                        font { pixelSize: 22; bold: true; family: zeroHour.name }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "rpm"
                        color: "#BBBBBB"
                        font { pixelSize: 13; family: zeroHour.name; letterSpacing: 3 }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 10
                        Canvas {
                            id: leftSignalCanvas
                            width: 54; height: 38
                            property bool   active:     vehicleModel.leftSignal
                            property real   glowPos:    0.0
                            property color  signalColor: root.themeColor
                            onActiveChanged:      requestPaint()
                            onGlowPosChanged:     requestPaint()
                            onSignalColorChanged: requestPaint()
                            Component.onCompleted: loadImage("qrc:/assets/LeftSignal.svg")
                            onImageLoaded: requestPaint()
                            NumberAnimation on glowPos {
                                from: 0.0; to: 1.0; duration: 600
                                loops: Animation.Infinite
                                running: leftSignalCanvas.active
                            }
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                if (!isImageLoaded("qrc:/assets/LeftSignal.svg") || !active) return
                                if (glowPos > 0.75) return
                                var fillW = (glowPos / 0.75) * width
                                ctx.save()
                                ctx.beginPath()
                                ctx.rect(width - fillW, 0, fillW, height)
                                ctx.clip()
                                ctx.drawImage("qrc:/assets/LeftSignal.svg", 0, 0, width, height)
                                ctx.globalCompositeOperation = "source-atop"
                                var sr = Math.round(signalColor.r * 255)
                                var sg = Math.round(signalColor.g * 255)
                                var sb = Math.round(signalColor.b * 255)
                                ctx.fillStyle = "rgb(" + sr + "," + sg + "," + sb + ")"
                                ctx.fillRect(0, 0, width, height)
                                ctx.restore()
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: vehicleModel.tripDistance.toFixed(1) + " km"
                                color: root.themeColor
                                font { pixelSize: 14; family: zeroHour.name }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "TRIP"; color: "#BBBBBB"
                                font { pixelSize: 10; family: zeroHour.name; letterSpacing: 3 }
                            }
                            Item { width: 1; height: 4 }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: vehicleModel.odometer + " km"
                                color: "#FFFFFF"
                                font { pixelSize: 18; family: zeroHour.name }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "ODO"; color: "#BBBBBB"
                                font { pixelSize: 10; family: zeroHour.name; letterSpacing: 3 }
                            }
                        }
                        Canvas {
                            id: rightSignalCanvas
                            width: 54; height: 38
                            property bool   active:     vehicleModel.rightSignal
                            property real   glowPos:    0.0
                            property color  signalColor: root.themeColor
                            onActiveChanged:      requestPaint()
                            onGlowPosChanged:     requestPaint()
                            onSignalColorChanged: requestPaint()
                            Component.onCompleted: loadImage("qrc:/assets/RightSignal.svg")
                            onImageLoaded: requestPaint()
                            NumberAnimation on glowPos {
                                from: 0.0; to: 1.0; duration: 600
                                loops: Animation.Infinite
                                running: rightSignalCanvas.active
                            }
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                if (!isImageLoaded("qrc:/assets/RightSignal.svg") || !active) return
                                if (glowPos > 0.75) return
                                var fillW = (glowPos / 0.75) * width
                                ctx.save()
                                ctx.beginPath()
                                ctx.rect(0, 0, fillW, height)
                                ctx.clip()
                                ctx.drawImage("qrc:/assets/RightSignal.svg", 0, 0, width, height)
                                ctx.globalCompositeOperation = "source-atop"
                                var sr = Math.round(signalColor.r * 255)
                                var sg = Math.round(signalColor.g * 255)
                                var sb = Math.round(signalColor.b * 255)
                                ctx.fillStyle = "rgb(" + sr + "," + sg + "," + sb + ")"
                                ctx.fillRect(0, 0, width, height)
                                ctx.restore()
                            }
                        }
                    }
                }
            }
        }

        // Gear selector — bottom strip
        Item {
            id: gearZone
            x: 210; y: 414
            width: 380; height: 36
            z: 3
            Row {
                anchors.centerIn: parent
                Repeater {
                    model: ["P", "R", "N", "D"]
                    delegate: Item {
                        width: 76; height: gearZone.height
                        property bool isActive: {
                            var g = vehicleModel.gear
                            if (modelData === "D") return g === "D" || g === "D1" || g === "D2" || g === "D3"
                            return g === modelData
                        }
                        // Separator: horizontalCenter on parent.left = exactly bisects the gap
                        Rectangle {
                            anchors { verticalCenter: parent.verticalCenter; horizontalCenter: parent.left }
                            visible: index > 0; width: 1; height: 18
                            color: Qt.rgba(root.themeColor.r, root.themeColor.g, root.themeColor.b, 0.30)
                        }
                        Rectangle {
                            anchors.centerIn: parent; width: 54; height: 30; radius: 15
                            opacity: isActive ? 1.0 : 0.0
                            color: Qt.rgba(root.themeColor.r, root.themeColor.g, root.themeColor.b, 0.10)
                            border.color: Qt.rgba(root.themeColor.r, root.themeColor.g, root.themeColor.b, 0.45)
                            border.width: 1
                            Behavior on opacity { NumberAnimation { duration: 160 } }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: isActive ? root.themeColor : "#BBBBBB"
                            font { pixelSize: 16; bold: isActive; family: zeroHour.name }
                            scale: isActive ? 1.06 : 1.0; transformOrigin: Item.Center
                            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                        }
                    }
                }
            }
        }

        Component.onCompleted: {
            console.log("=== [VCU V10] ===")
            console.log("[Font] ZeroHour: " + (zeroHour.status === FontLoader.Ready ? "OK" : "FAILED"))
            Qt.callLater(function() {
                function st(s) { return s === Image.Ready ? "OK" : "FAIL(" + s + ")" }
                console.log("[PNG] bg=" + st(bgImg.status) + " car=" + st(carImg.status))
                console.log("[SVG] speedTrack=" + st(speedTrackImg.status) + " rpmTrack=" + st(rpmTrackImg.status) + " batt=" + st(batteryTrackImg.status))
                console.log("[Scale] " + root.uniformScale.toFixed(4))
            })
        }
    } // dashboardScene

    // ── FPS counter ───────────────────────────────────────────────────────────
    Item {
        id: fpsOverlay
        x: 4; y: 4; z: 9999
        width: 70; height: 16

        property real _fps: 0.0
        property int  _cnt: 0
        property real _ts:  0.0
        property real fpsTick: 0

        // NumberAnimation fires onFpsTickChanged exactly once per render frame.
        // Canvas + requestAnimationFrame is unreliable on Qt5/EGLFS for this use.
        NumberAnimation on fpsTick {
            from: 0; to: 1; duration: 1000
            loops: Animation.Infinite; running: true
        }

        onFpsTickChanged: {
            _cnt++
            var now = Date.now()
            if (_ts === 0) { _ts = now }
            if (now - _ts >= 1000) {
                _fps = _cnt * 1000.0 / (now - _ts)
                _cnt = 0; _ts = now
            }
        }

        Text {
            anchors.fill: parent
            text: fpsOverlay._fps.toFixed(0) + " fps"
            color: fpsOverlay._fps >= 55 ? "#00FF00"
                 : fpsOverlay._fps >= 30 ? "#FFCC00"
                 : "#FF3333"
            font { pixelSize: 11; family: zeroHour.name }
        }
    }
}
