import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    implicitWidth: 900
    implicitHeight: 620
    color: "#0b0d10"

    readonly property var backend:
        logos ? logos.module("observer_ui") : null


    property bool advancedSettingsExpanded: false

    property int currentEpoch: Math.floor(Date.now() / 1000)

    Timer {
        interval: 1000
        repeat: true

        running: !!root.backend
            && root.backend.pairingExpiresAt > 0

        onTriggered: {
            root.currentEpoch = Math.floor(Date.now() / 1000)
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        anchors.margins: 24
        clip: true

        ColumnLayout {
            width: pageScroll.availableWidth
            spacing: 20

        Label {
            text: "Observer"
            font.pixelSize: 28
            font.bold: true
            color: "#ffffff"
        }

        Label {
            text: "Service monitoring and configuration"
            color: "#9ca3af"
            font.pixelSize: 14
        }


            GroupBox {
                id: setupReadinessPanel
                title: "Setup Readiness"
                Layout.fillWidth: true

                property bool showChecks: false
                property string lastHttpsCheckedInputs: ""

                readonly property string currentHttpsInputs:
                    endpointField.text.trim() + "\\u001f"
                    + certField.text.trim() + "\\u001f"
                    + pinField.text.trim()

                readonly property var checks: {
                    const b = root.backend;
                    if (!b) return [];

                    const service = b.serviceStatus;
                    const sdk = b.chatSdkStatus;
                    const chat = b.chatStatus;
                    const conv = b.conversationsStatus;
                    const sdkReady = sdk === "Responding";
                    const chatReady =
                        sdkReady && chat === "Available";
                    const https = b.healthStatus;
                    const httpsCurrent =
                        setupReadinessPanel.lastHttpsCheckedInputs
                        === setupReadinessPanel.currentHttpsInputs;

                    return [
                        {
                            name: "Observer Service",
                            state: service === "running" ? "ready"
                                : (service === "stopped" || service === "error")
                                  ? "failed" : "unknown",
                            detail: service === "running"
                                ? "Managed process is running"
                                : "Start Observer using the control below"
                        },
                        {
                            name: "LAN & HTTPS",
                            state: !httpsCurrent || b.healthBusy
                                ? "unknown"
                                : https === "Healthy"
                                  ? "ready"
                                  : (https === "Unavailable or unhealthy"
                                     || https === "Module call failed"
                                     || https === "Invalid response")
                                    ? "failed" : "unknown",
                            detail: !httpsCurrent
                                ? "Run setup checks to verify HTTPS"
                                : https === "Healthy"
                                  ? "HTTPS and Observer identity verified"
                                  : https === "Configuration required"
                                    ? "Enter endpoint, certificate path, "
                                      + "and SPKI pin below"
                                    : https === "Checking..."
                                      ? "Checking HTTPS connection..."
                                      : "Review Observer Connection "
                                        + "settings and retry"
                        },
                        {
                            name: "Authorized Devices",
                            state: b.deviceBusy
                                ? "unknown"
                                : b.deviceStatus === "Ready"
                                  ? "ready"
                                  : b.deviceStatus === "Error"
                                    ? "failed" : "unknown",
                            detail: b.deviceStatus === "Ready"
                                ? b.deviceDetails
                                : b.deviceStatus === "Unpaired"
                                  ? "Pair a device using the QR code below"
                                  : b.deviceStatus === "Error"
                                    ? b.deviceDetails
                                    : "Run setup checks to verify authorization"
                        },
                        {
                            name: "Native Chat SDK",
                            state: sdk === "Responding" ? "ready"
                                : (sdk === "Unavailable"
                                   || sdk === "Timed out")
                                  ? "failed" : "unknown",
                            detail: sdk === "Responding"
                                ? "Native Chat SDK is responding"
                                : "Check the native Chat SDK connection"
                        },
                        {
                            name: "Basecamp Chat",
                            state: !sdkReady ? "unknown"
                                : chat === "Available" ? "ready"
                                : chat === "Unavailable"
                                  ? "failed" : "unknown",
                            detail: !sdkReady
                                ? "Waiting for native Chat SDK"
                                : chat === "Available"
                                  ? "Chat has initialized successfully"
                                  : "Open Basecamp Chat once"
                        },
                        {
                            name: "Conversations",
                            state: !chatReady ? "unknown"
                                : conv.indexOf("Readable") === 0
                                  ? "ready"
                                  : (conv === "Unavailable"
                                     || conv === "Timed out")
                                    ? "failed" : "unknown",
                            detail: !sdkReady
                                ? "Waiting for native Chat SDK"
                                : !chatReady
                                  ? "Waiting for Basecamp Chat"
                                  : conv.indexOf("Readable") === 0
                                    ? conv
                                    : "Recheck Chat conversations"
                        }
                    ];
                }

                readonly property int readyCount:
                    checks.filter(function(item) {
                        return item.state === "ready";
                    }).length

                label: Label {
                    text: "Setup Readiness"
                    color: "#e2e8f0"
                    font.pixelSize: 15
                    font.bold: true
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14

                    Label {
                        Layout.fillWidth: true
                        text: setupReadinessPanel.readyCount === 6
                            ? "All systems ready - 6/6"
                            : setupReadinessPanel.readyCount
                              + " of 6 checks passing"
                        font.pixelSize: 17
                        font.bold: true
                        color: setupReadinessPanel.readyCount === 6
                            ? "#4ade80" : "#e2e8f0"
                    }

                    Button {
                        text: setupReadinessPanel.showChecks
                            ? "Hide system checks"
                            : "View system checks"
                        visible: setupReadinessPanel.readyCount === 6
                        onClicked: setupReadinessPanel.showChecks =
                            !setupReadinessPanel.showChecks
                    }

                    Repeater {
                        model: setupReadinessPanel.checks

                        delegate: RowLayout {
                            visible: setupReadinessPanel.showChecks || setupReadinessPanel.readyCount !== 6
                            Layout.fillWidth: true
                            spacing: 12

                            readonly property var check: modelData

                            Rectangle {
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                radius: 7
                                color: check.state === "ready"
                                    ? "#22c55e"
                                    : check.state === "failed"
                                      ? "#ef4444" : "#64748b"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Label {
                                    text: check.name
                                    color: "#e2e8f0"
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: check.detail
                                    color: "#94a3b8"
                                    wrapMode: Text.Wrap
                                }

                                Label {
                                    Layout.fillWidth: true

                                    visible: check.state !== "ready"
                                        && (check.name === "Native Chat SDK"
                                            || (check.name === "Basecamp Chat"
                                                && root.backend
                                                && root.backend.chatSdkStatus
                                                   === "Responding")
                                            || check.name === "LAN & HTTPS")

                                    text: check.name === "Native Chat SDK"
                                        ? "Verify the Chat module is loaded, "
                                          + "then "
                                          + "run setup checks again."
                                        : check.name === "Basecamp Chat"
                                          ? "Open Basecamp Chat once to "
                                            + "initialize its backend. "
                                            + "The Chat window can be closed "
                                            + "after initialization."
                                          : "Leave the connection fields "
                                            + "empty to verify the managed "
                                            + "Observer automatically."

                                    color: "#cbd5e1"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }

                                Button {
                                    Layout.alignment: Qt.AlignLeft

                                    visible: check.state !== "ready"
                                        && (check.name === "Observer Service"
                                            || check.name === "Authorized Devices"
                                            || (check.name === "Conversations"
                                                && root.backend
                                                && root.backend.chatSdkStatus
                                                   === "Responding"
                                                && root.backend.chatStatus
                                                   === "Available"))

                                    text: check.name === "Observer Service"
                                        ? "Start Observer"
                                        : check.name === "Authorized Devices"
                                          ? "Generate Pairing QR"
                                          : "Recheck Chat"

                                    enabled: !!root.backend
                                        && root.backend.ready
                                        && (
                                            check.name === "Observer Service"
                                            ? !root.backend.serviceBusy
                                              && root.backend.serviceStatus
                                                 !== "running"
                                              && root.backend.serviceStatus
                                                 !== "starting"
                                            : check.name === "Authorized Devices"
                                              ? root.backend.serviceStatus
                                                === "running"
                                                && !root.backend.pairingBusy
                                                && !root.backend.serviceBusy
                                              : !root.backend.chatBusy
                                        )

                                    onClicked: {
                                        if (check.name === "Observer Service") {
                                            root.backend.startService();
                                        } else if (
                                            check.name === "Authorized Devices"
                                        ) {
                                            root.backend.createPairingOffer();
                                        } else {
                                            root.backend.checkChatHealth();
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: check.state !== "ready"
                                        && check.name === "Authorized Devices"

                                    text: "Scan the QR code in the "
                                          + "Pair Device section below. "
                                          + "After pairing, run setup checks."

                                    color: "#94a3b8"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Button {
                        text: "Run Setup Checks"
                        enabled: !!root.backend
                            && root.backend.ready
                            && !root.backend.serviceBusy
                            && !root.backend.chatBusy
                            && !root.backend.healthBusy
                            && !root.backend.deviceBusy

                        onClicked: {
                            setupReadinessPanel.lastHttpsCheckedInputs =
                                setupReadinessPanel.currentHttpsInputs;

                            root.backend.refreshServiceStatus();
                            root.backend.checkChatHealth();
                            root.backend.checkDeviceSummary();
                            root.backend.checkHealth(
                                endpointField.text,
                                certField.text,
                                pinField.text
                            );
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: setupReadinessPanel.readyCount === 6
                            ? "Local Observer, Chat, and HTTPS checks passed. "
                              + "Verify device access separately."
                            : "Follow the instructions beside each "
                              + "incomplete check, then run checks again."
                        color: "#94a3b8"
                        wrapMode: Text.Wrap
                    }
                }
            }

        GroupBox {
            title: "Your Devices"
            Layout.fillWidth: true

            label: Label {
                text: "Your Devices"
                color: "#e2e8f0"
                font.pixelSize: 15
                font.bold: true
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    text: "Generate a five-minute pairing offer for read-only Chat access."
                    color: "#cbd5e1"
                    wrapMode: Text.Wrap
                }

                Label {
                    text: "Permission: chat.read"
                    color: "#94a3b8"
                }

                Button {
                    text: root.backend && root.backend.pairingBusy
                        ? "Creating..."
                        : "Pair New Device"

                    enabled: !!root.backend
                        && root.backend.ready
                        && !root.backend.serviceBusy
                        && !root.backend.pairingBusy
                        && root.backend.serviceStatus === "running"

                    onClicked: root.backend.createPairingOffer()
                }

                Label {
                    Layout.fillWidth: true
                    text: root.backend
                        ? root.backend.deviceStatus === "Ready"
                          ? root.backend.deviceDetails
                          : root.backend.deviceStatus === "Unpaired"
                            ? "No devices authorized yet"
                            : root.backend.deviceStatus === "Error"
                              ? root.backend.deviceDetails
                              : "Device authorization not checked"
                        : "Backend unavailable"
                    color: "#e2e8f0"
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !!root.backend
                        && root.backend.pairingStatus !== "Not paired"
                        && root.backend.pairingStatus !== "Not checked"

                    text: root.backend
                        ? "Pairing: " + root.backend.pairingStatus
                        : ""

                    color: "#94a3b8"
                    wrapMode: Text.Wrap
                }

                Canvas {
                    id: pairingCanvas

                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 320
                    Layout.preferredHeight: 320

                    property string matrix: root.backend
                        ? root.backend.pairingQrMatrix
                        : ""

                    visible: matrix.length > 0
                        && root.backend.pairingExpiresAt > root.currentEpoch

                    onMatrixChanged: requestPaint()

                    onPaint: {
                        const ctx = getContext("2d");

                        ctx.fillStyle = "white";
                        ctx.fillRect(0, 0, width, height);

                        if (!matrix.length)
                            return;

                        const rows = matrix.split("\n");
                        const size = rows.length;
                        const quiet = 4;

                        const cell = Math.floor(
                            Math.min(width, height)
                            / (size + quiet * 2)
                        );

                        if (cell < 1)
                            return;

                        const offsetX = Math.floor(
                            (width - cell * (size + quiet * 2)) / 2
                        );

                        const offsetY = Math.floor(
                            (height - cell * (size + quiet * 2)) / 2
                        );

                        ctx.fillStyle = "black";

                        for (let y = 0; y < size; ++y) {
                            for (let x = 0; x < size; ++x) {
                                if (rows[y][x] === "1") {
                                    ctx.fillRect(
                                        offsetX + (x + quiet) * cell,
                                        offsetY + (y + quiet) * cell,
                                        cell,
                                        cell
                                    );
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter

                    visible: !!root.backend
                        && root.backend.pairingExpiresAt > 0

                    text: "Expires in "
                        + Math.max(
                            0,
                            root.backend.pairingExpiresAt
                                - root.currentEpoch
                        )
                        + " seconds"

                    color: "#94a3b8"
                }

                Label {
                    Layout.fillWidth: true
                    text: "Scan only with a trusted Observer client. "
                        + "The QR contains a temporary pairing secret."
                    color: "#94a3b8"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignLeft
            flat: false
            font.bold: true
            text: root.advancedSettingsExpanded
                ? "Hide advanced settings"
                : "Advanced settings and diagnostics"
            onClicked: root.advancedSettingsExpanded =
                !root.advancedSettingsExpanded
        }

        GroupBox {
            title: "Managed Observer"
            visible: root.advancedSettingsExpanded
            Layout.fillWidth: true

            label: Label {
                text: "Managed Observer"
                color: "#e2e8f0"
                font.pixelSize: 15
                font.bold: true
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "Service: " + (root.backend
                        ? root.backend.serviceStatus
                        : "Backend unavailable")
                    color: "#e2e8f0"
                    font.pixelSize: 16
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    text: root.backend
                        ? root.backend.serviceDetails
                        : ""
                    color: "#94a3b8"
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    spacing: 12

                    Button {
                        text: "Start Observer"
                        enabled: !!root.backend
                            && root.backend.ready
                            && !root.backend.serviceBusy
                            && root.backend.serviceStatus !== "running"
                            && root.backend.serviceStatus !== "starting"

                        onClicked: root.backend.startService()
                    }

                    Button {
                        text: "Stop Observer"
                        enabled: !!root.backend
                            && root.backend.ready
                            && !root.backend.serviceBusy
                            && (root.backend.serviceStatus === "running"
                                || root.backend.serviceStatus === "starting")

                        onClicked: root.backend.stopService()
                    }

                    Button {
                        text: root.backend && root.backend.serviceBusy
                            ? "Working..."
                            : "Refresh Status"

                        enabled: !!root.backend
                            && root.backend.ready
                            && !root.backend.serviceBusy

                        onClicked: root.backend.refreshServiceStatus()
                    }
                }


            }
        }

        GroupBox {
            title: "Observer Connection"
            visible: root.advancedSettingsExpanded
            label: Label {
                text: "Observer Connection"
                color: "#e2e8f0"
                font.pixelSize: 15
                font.bold: true
            }
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: "Observer HTTPS Health URL"
                    color: "#cbd5e1"
                }

                TextField {
                    id: endpointField
                    implicitHeight: 42
                    color: "#f8fafc"
                    placeholderTextColor: "#94a3b8"
                    selectionColor: "#256f7c"
                    selectedTextColor: "#ffffff"
                    background: Rectangle {
                        color: "#1e293b"
                        radius: 6
                        border.color: "#475569"
                    }
                    Layout.fillWidth: true
                    placeholderText: "https://192.168.x.x:8444/health"
                }

                Label {
                    text: "Trusted TLS Certificate Path"
                    color: "#cbd5e1"
                }

                TextField {
                    id: certField
                    implicitHeight: 42
                    color: "#f8fafc"
                    placeholderTextColor: "#94a3b8"
                    selectionColor: "#256f7c"
                    selectedTextColor: "#ffffff"
                    background: Rectangle {
                        color: "#1e293b"
                        radius: 6
                        border.color: "#475569"
                    }
                    Layout.fillWidth: true
                    placeholderText: "/path/to/observer-v2-cert.pem"
                }

                Label {
                    text: "Observer SPKI Pin"
                    color: "#cbd5e1"
                }

                TextField {
                    id: pinField
                    implicitHeight: 42
                    color: "#f8fafc"
                    placeholderTextColor: "#94a3b8"
                    selectionColor: "#256f7c"
                    selectedTextColor: "#ffffff"
                    background: Rectangle {
                        color: "#1e293b"
                        radius: 6
                        border.color: "#475569"
                    }
                    Layout.fillWidth: true
                    placeholderText: "sha256/..."
                }

                Button {
                    id: healthButton

                    implicitHeight: 42
                    leftPadding: 20
                    rightPadding: 20

                    contentItem: Text {
                        text: healthButton.text
                        color: healthButton.enabled ? "#08141c" : "#94a3b8"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: healthButton.enabled ? "#67dbe6" : "#293444"
                        radius: 6
                    }

                    text: root.backend && root.backend.healthBusy
                          ? "Checking..."
                          : "Check Health"

                    enabled: !!root.backend
                             && root.backend.ready
                             && !root.backend.healthBusy
                             && endpointField.text.trim().length > 0
                             && certField.text.trim().length > 0
                             && pinField.text.trim().length > 0

                    onClicked: {
                        root.backend.checkHealth(
                            endpointField.text.trim(),
                            certField.text.trim(),
                            pinField.text.trim()
                        )
                    }
                }
            }
        }


        GroupBox {
            title: "Native Chat SDK (test)"
            visible: root.advancedSettingsExpanded
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Label {
                    text: "Native status: " + (root.backend
                        ? root.backend.nativeChatStatus
                        : "Backend unavailable")
                    color: "#e2e8f0"
                    font.bold: true
                }

                Button {
                    text: root.backend && root.backend.nativeChatBusy
                        ? "Checking..."
                        : "Test Native Chat"
                    enabled: !!root.backend
                        && root.backend.ready
                        && !root.backend.nativeChatBusy
                    onClicked: root.backend.checkNativeChatStatus()
                }

                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    readOnly: true
                    text: root.backend
                        ? root.backend.nativeChatDetails : ""
                    color: "#cbd5e1"
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    background: Rectangle {
                        color: "#161f2c"
                        radius: 6
                    }
                }
            }
        }
        GroupBox {
            title: "Service Health"
            visible: root.advancedSettingsExpanded
            label: Label {
                text: "Service Health"
                color: "#e2e8f0"
                font.pixelSize: 15
                font.bold: true
            }
            Layout.fillWidth: true
            Layout.preferredHeight: 340

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Label {
                    text: root.backend
                          ? root.backend.healthStatus
                          : "Backend unavailable"

                    font.pixelSize: 18
                    font.bold: true
                    color: "#e2e8f0"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    Label {
                        text: "Native Chat SDK: " + (root.backend
                            ? root.backend.chatSdkStatus
                            : "Unknown")
                        color: "#cbd5e1"
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "Basecamp Chat: " + (root.backend
                            ? root.backend.chatStatus
                            : "Unknown")
                        color: "#cbd5e1"
                        Layout.fillWidth: true
                    }

                    Button {
                        text: root.backend && root.backend.chatBusy
                            ? "Checking..."
                            : "Check Chat"
                        enabled: !!root.backend
                            && root.backend.ready
                            && !root.backend.chatBusy
                        onClicked: root.backend.checkChatHealth()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Conversations: " + (root.backend
                        ? root.backend.conversationsStatus
                        : "Unknown")
                    color: "#cbd5e1"
                    font.pixelSize: 14
                }

                Label {
                    Layout.fillWidth: true
                    text: root.backend ? root.backend.chatDetails : ""
                    color: "#94a3b8"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextArea {
                        text: root.backend
                              ? root.backend.healthDetails
                              : ""

                        readOnly: true
                        color: "#cbd5e1"
                        font.pixelSize: 14
                        background: Rectangle {
                            color: "#161f2c"
                            radius: 6
                        }
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                    }
                }
            }
        }
        }
    }
}
