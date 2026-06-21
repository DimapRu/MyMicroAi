#include "AppStateMachine.h"
#include <SD.h>

namespace {
const char* modeName(AppMode mode) {
    switch (mode) {
        case AppMode::Boot: return "Boot";
        case AppMode::Configuration: return "Configuration";
        case AppMode::Work: return "Work";
        case AppMode::Error: return "Error";
    }
    return "Unknown";
}

void removeLastUtf8Character(String& text) {
    if (text.length() == 0) {
        return;
    }

    int index = static_cast<int>(text.length()) - 1;
    while (index > 0 && (static_cast<uint8_t>(text[index]) & 0xC0) == 0x80) {
        --index;
    }
    text.remove(index);
}
}

void AppStateMachine::begin() {
    Serial.println(F("MyMicroAI boot sequence started"));
    display_.begin();
    display_.showBoot("Booting system...");

    if (!psramFound()) {
        enterError(AppError::PsramUnavailable, "PSRAM is not available");
        return;
    }

    Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

    display_.showBoot("Initializing SD card...");
    if (!storage_.begin()) {
        Serial.printf("Storage warning: %s\n", storage_.lastError());
        enterConfigurationMode();
        return;
    }

    if (!config_.load(storage_)) {
        Serial.printf("Config warning: %s\n", config_.lastError());
        enterConfigurationMode();
        return;
    }

    display_.showBoot("Configuration loaded");
    enterWorkMode();
}

void AppStateMachine::update() {
    if (mode_ == AppMode::Configuration) {
        network_.handleClient();
        return;
    }

    if (mode_ == AppMode::Work) {
        updateWorkMode();
    }
}

void AppStateMachine::enterConfigurationMode() {
    mode_ = AppMode::Configuration;
    lastSettingsTouchMs_ = millis();
    Serial.println(F("Entering Configuration Mode"));
    network_.beginConfigPortal(config_, storage_);
    const String address = network_.localAddress();
    display_.showConfigurationMode(address);
    Serial.printf("SoftAP address: http://%s/\n", address.c_str());
}

void AppStateMachine::enterWorkMode() {
    mode_ = AppMode::Work;
    Serial.println(F("Entering Work Mode"));

    if (!network_.beginStation(config_.config(), 20000)) {
        enterError(AppError::WifiFailed, network_.lastError());
        return;
    }

    if (!chat_.begin(storage_)) {
        enterError(AppError::StorageUnavailable, chat_.lastError());
        return;
    }

    cleanupPendingPhotos();
    inputText_ = "";
    workStatus_ = "Ready";
    keyboardVisible_ = false;
    chatsMenuVisible_ = false;
    clearChatsConfirmVisible_ = false;
    chatsMenuOffset_ = 0;
    chatScrollOffset_ = 0;
    cameraModeVisible_ = false;
    lastCameraFrameMs_ = 0;
    workUiDirty_ = true;
    redrawWorkMode();
    Serial.printf("Connected. IP: %s\n", network_.localAddress().c_str());
}

void AppStateMachine::updateWorkMode() {
    if (!network_.isStationConnected()) {
        enterError(AppError::WifiFailed, "WiFi connection lost");
        return;
    }

    if (cameraModeVisible_) {
        redrawCameraPreview(false);
    }

    ChatUiAction action;
    if (display_.pollChatAction(action)) {
        switch (action.type) {
            case ChatUiActionType::Settings:
                if (millis() - lastSettingsTouchMs_ > 1200) {
                    lastSettingsTouchMs_ = millis();
                    Serial.println(F("Work Mode settings button pressed"));
                    enterConfigurationMode();
                }
                return;
            case ChatUiActionType::NewChat:
                if (chat_.createChat()) {
                    inputText_ = "";
                    chatsMenuOffset_ = 0;
                    chatScrollOffset_ = 0;
                    workStatus_ = F("New chat");
                } else {
                    workStatus_ = chat_.lastError();
                }
                workUiDirty_ = true;
                break;
            case ChatUiActionType::OpenCamera:
                enterCameraMode();
                return;
            case ChatUiActionType::CloseCamera:
                exitCameraMode();
                return;
            case ChatUiActionType::CapturePhoto:
                capturePhotoToQueue();
                return;
            case ChatUiActionType::OpenChats:
                keyboardVisible_ = false;
                chatsMenuVisible_ = true;
                chatsMenuOffset_ = max<int>(0, chat_.activeIndex());
                workUiDirty_ = true;
                break;
            case ChatUiActionType::CloseChats:
                chatsMenuVisible_ = false;
                workUiDirty_ = true;
                break;
            case ChatUiActionType::ChatsUp:
                if (chatsMenuOffset_ > 0) {
                    --chatsMenuOffset_;
                    workUiDirty_ = true;
                }
                break;
            case ChatUiActionType::ChatsDown:
                if (chatsMenuOffset_ + 4 < static_cast<int>(chat_.sessions().size())) {
                    ++chatsMenuOffset_;
                    workUiDirty_ = true;
                }
                break;
            case ChatUiActionType::SelectChat:
                if (action.index >= 0 && chat_.switchChat(action.index)) {
                    cleanupPendingPhotos();
                    inputText_ = "";
                    chatsMenuVisible_ = false;
                    chatScrollOffset_ = 0;
                    workStatus_ = F("Chat switched");
                } else {
                    workStatus_ = chat_.lastError();
                }
                workUiDirty_ = true;
                break;
            case ChatUiActionType::OpenClearChatsConfirm:
                keyboardVisible_ = false;
                chatsMenuVisible_ = false;
                clearChatsConfirmVisible_ = true;
                workUiDirty_ = true;
                break;
            case ChatUiActionType::CancelClearChats:
                clearChatsConfirmVisible_ = false;
                workUiDirty_ = true;
                break;
            case ChatUiActionType::ConfirmClearChats:
                if (chat_.clearAllChats()) {
                    cleanupPendingPhotos();
                    inputText_ = "";
                    chatsMenuVisible_ = false;
                    clearChatsConfirmVisible_ = false;
                    chatsMenuOffset_ = 0;
                    chatScrollOffset_ = 0;
                    workStatus_ = F("All chats cleared");
                } else {
                    clearChatsConfirmVisible_ = false;
                    workStatus_ = chat_.lastError();
                }
                workUiDirty_ = true;
                break;
            case ChatUiActionType::OpenKeyboard:
                keyboardVisible_ = true;
                clearChatsConfirmVisible_ = false;
                workUiDirty_ = true;
                break;
            case ChatUiActionType::CloseKeyboard:
                keyboardVisible_ = false;
                workUiDirty_ = true;
                break;
            case ChatUiActionType::Character:
                if (inputText_.length() + action.text.length() < 220) {
                    inputText_ += action.text;
                    if (keyboardVisible_) {
                        display_.refreshKeyboardPreview(inputText_);
                    } else {
                        workUiDirty_ = true;
                    }
                }
                break;
            case ChatUiActionType::LayoutSwitch:
                workUiDirty_ = true;
                break;
            case ChatUiActionType::Backspace:
                if (inputText_.length() > 0) {
                    removeLastUtf8Character(inputText_);
                    if (keyboardVisible_) {
                        display_.refreshKeyboardPreview(inputText_);
                    } else {
                        workUiDirty_ = true;
                    }
                }
                break;
            case ChatUiActionType::Send:
                sendCurrentMessage();
                workUiDirty_ = true;
                break;
            case ChatUiActionType::ScrollUp:
                chatScrollOffset_ += 28;
                if (chatScrollOffset_ > 1200) {
                    chatScrollOffset_ = 1200;
                }
                workUiDirty_ = true;
                break;
            case ChatUiActionType::ScrollDown:
                chatScrollOffset_ -= 28;
                if (chatScrollOffset_ < 0) {
                    chatScrollOffset_ = 0;
                }
                workUiDirty_ = true;
                break;
            case ChatUiActionType::None:
                break;
        }
    }

    if (workUiDirty_) {
        redrawWorkMode();
    }
}

void AppStateMachine::redrawWorkMode() {
    display_.showChatWorkMode(chat_.sessions(), chat_.activeIndex(), chat_.messages(), inputText_, keyboardVisible_, workStatus_, chatScrollOffset_, chatsMenuVisible_, chatsMenuOffset_, clearChatsConfirmVisible_);
    workUiDirty_ = false;
}

void AppStateMachine::redrawCameraPreview(bool forceFrame) {
    if (!cameraModeVisible_) {
        return;
    }

    const uint32_t now = millis();
    if (!forceFrame && now - lastCameraFrameMs_ < 190) {
        return;
    }

    camera_.returnFrame(previewFrame_);
    previewFrame_ = camera_.capturePreviewFrame();
    lastCameraFrameMs_ = now;
    display_.showCameraPreview(previewFrame_, pendingPhotos_.size(), workStatus_, forceFrame);
}

void AppStateMachine::enterCameraMode() {
    keyboardVisible_ = false;
    chatsMenuVisible_ = false;
    clearChatsConfirmVisible_ = false;
    workStatus_ = F("Starting camera...");
    cameraModeVisible_ = true;
    if (!camera_.begin()) {
        cameraModeVisible_ = false;
        workStatus_ = camera_.lastError();
        workUiDirty_ = true;
        return;
    }
    workStatus_ = F("Ready");
    redrawCameraPreview(true);
}

void AppStateMachine::exitCameraMode() {
    cameraModeVisible_ = false;
    camera_.returnFrame(previewFrame_);
    previewFrame_ = nullptr;
    workStatus_ = F("Ready");
    workUiDirty_ = true;
}

void AppStateMachine::capturePhotoToQueue() {
    if (!cameraModeVisible_) {
        return;
    }

    camera_.returnFrame(previewFrame_);
    previewFrame_ = nullptr;
    workStatus_ = F("Capturing...");
    display_.showCameraPreview(nullptr, pendingPhotos_.size(), workStatus_, true);
    previewFrame_ = camera_.capturePhotoFrame();
    if (!previewFrame_) {
        cameraModeVisible_ = false;
        workStatus_ = camera_.lastError();
        workUiDirty_ = true;
        redrawWorkMode();
        return;
    }

    if (!SD.exists("/photos")) {
        SD.mkdir("/photos");
    }

    const String label = String(F("photo")) + String(nextPhotoId_++);
    const String path = String(F("/photos/")) + label + F(".jpg");
    if (!camera_.saveFrameToFile(previewFrame_, path.c_str())) {
        cameraModeVisible_ = false;
        workStatus_ = camera_.lastError();
        camera_.returnFrame(previewFrame_);
        previewFrame_ = nullptr;
        workUiDirty_ = true;
        redrawWorkMode();
        return;
    }

    pendingPhotos_.push_back(PhotoAttachment{label, path});
    if (inputText_.length() > 0 && !inputText_.endsWith(" ")) {
        inputText_ += " ";
    }
    inputText_ += label;
    workStatus_ = label + F(" queued");
    cameraModeVisible_ = false;
    camera_.returnFrame(previewFrame_);
    previewFrame_ = nullptr;
    lastCameraFrameMs_ = 0;
    workUiDirty_ = true;
    redrawWorkMode();
}

void AppStateMachine::cleanupPendingPhotos() {
    for (const PhotoAttachment& photo : pendingPhotos_) {
        if (photo.path.length() > 0 && SD.exists(photo.path.c_str())) {
            SD.remove(photo.path.c_str());
        }
    }
    pendingPhotos_.clear();
    nextPhotoId_ = 1;
}

void AppStateMachine::sendCurrentMessage() {
    inputText_.trim();
    if (inputText_.length() == 0 && pendingPhotos_.empty()) {
        workStatus_ = F("Type a message first");
        return;
    }

    const String userText = inputText_;
    inputText_ = "";
    keyboardVisible_ = false;
    chatScrollOffset_ = 0;
    workStatus_ = F("Sending...");
    redrawWorkMode();

    if (!chat_.appendMessage("user", userText)) {
        workStatus_ = chat_.lastError();
        return;
    }

    redrawWorkMode();
    String assistantReply;
    if (!ai_.sendChatWithImages(config_.config(), chat_.messages(), pendingPhotos_, assistantReply)) {
        workStatus_ = ai_.lastError();
        chat_.appendMessage("assistant", String(F("Error: ")) + workStatus_);
        return;
    }

    if (!chat_.appendMessage("assistant", assistantReply)) {
        workStatus_ = chat_.lastError();
        return;
    }

    cleanupPendingPhotos();
    workStatus_ = F("Ready");
}

void AppStateMachine::enterError(AppError error, const char* message) {
    error_ = error;
    mode_ = AppMode::Error;
    display_.showError(error, message);
    Serial.printf("MyMicroAI error [%s]: %s\n", modeName(mode_), message);
}
