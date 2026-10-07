#include "Config.h"
#include "Main.h"
#include "NoteBotPlayer.h"
#include "NoteBotUI.h"
#include "NoteBlockRenderer.h"
#include "NoteBlockInteraction.h"
#include "ImGuiManager.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/deps/input/HIDController.h"
#include "mc/deps/input/InputHandler.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/packet/InventoryTransactionPacket.h"
#include "mc/network/packet/PlayerActionPacket.h"
#include "mc/network/packet/BlockEventPacket.h"

#include <imgui.h>
#include <variant>

namespace notebot {

extern std::unique_ptr<NoteBotPlayer> gPlayer;
extern std::unique_ptr<NoteBotUI>     gUI;
extern std::unique_ptr<NoteBlockRenderer> gRenderer;

static ll::event::ListenerPtr keyListener;
static ll::event::ListenerPtr mouseListener;
static std::atomic<bool> uiMouseActive{false};
static std::atomic<bool> restoreGameMouse{false};

static void syncUiMouse(IClientInstance& client) {
    const bool visible = gUI && gUI->isVisible();
    const bool wasActive = uiMouseActive.exchange(visible, std::memory_order_acq_rel);
    if (visible) {
        if (!wasActive) restoreGameMouse.store(false, std::memory_order_release);
        if (client.getMouseGrabbed()) {
            restoreGameMouse.store(true, std::memory_order_release);
            client.releaseMouse();
        }
    } else if (wasActive) {
        const bool shouldRestore = restoreGameMouse.exchange(false, std::memory_order_acq_rel);
        if (shouldRestore && client.isInGameInputEnabled() && !client.getMouseGrabbed())
            client.grabMouse();
    }
}

void RenderNoteBotUI() {
    if (gUI) {
        gUI->render();
    }

    if (gRenderer && gPlayer) {
        const auto& config = gPlayer->getConfig();
        gRenderer->render(gPlayer->getNoteBlockManager(), config,
                          gPlayer->getDisplayState().isPlaying);
    }
}

void installInputHooks() {
    auto& bus = ll::event::EventBus::getInstance();
    auto& logger = NoteBot::getInstance().getSelf().getLogger();

    keyListener = bus.emplaceListener<ll::event::input::KeyInputEvent>([&logger](auto& event) {
        if (!event.isDown()) return;
        if (event.controller().mTextboxIsFocused || event.controller().mTextboxIsSelected) return;

        auto& cfg = getConfig();
        if (event.keyCode() == cfg.keybinds.hotkey) {
            logger.info("Hotkey pressed (0x{:X}), toggling UI", event.keyCode());
            if (gUI) {
                gUI->toggle();
                if (auto client = ll::service::getClientInstance()) syncUiMouse(*client);
                logger.info("UI visibility: {}", gUI->isVisible());
            } else {
                logger.warn("gUI is null");
            }
            event.cancel();
        }

        if (gUI && gUI->isVisible() && event.keyCode() == 0x1B) {
            event.cancel();
        }
    });

    mouseListener = bus.emplaceListener<ll::event::input::MouseInputEvent>([](auto& event) {
        if (gUI && gUI->isVisible()) event.cancel();
    });

    logger.info("Input hooks installed");
}

void uninstallInputHooks() {
    if (gUI) gUI->setVisible(false);
    if (auto client = ll::service::getClientInstance()) syncUiMouse(*client);
    if (keyListener) {
        ll::event::EventBus::getInstance().removeListener(keyListener);
        keyListener.reset();
    }
    if (mouseListener) {
        ll::event::EventBus::getInstance().removeListener(mouseListener);
        mouseListener.reset();
    }
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBotInputTickHook,
    ll::memory::HookPriority::Normal,
    InputHandler,
    &InputHandler::tick,
    void,
    IMinecraftGame* mcGame,
    IClientInstance& client,
    Bedrock::NotNullNonOwnerPtr<ControllerIDtoClientMap> const& controllerClientMap,
    bool allowMultipleClients
) {
    syncUiMouse(client);
    static thread_local bool inNoteBotTick = false;
    if (!inNoteBotTick && gPlayer) {
        inNoteBotTick = true;
        struct ResetTickGuard {
            bool& flag;
            ~ResetTickGuard() { flag = false; }
        } reset{inNoteBotTick};
        gPlayer->tick();
    }
    origin(mcGame, client, controllerClientMap, allowMultipleClients);
    syncUiMouse(client);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBotUiMouseGrabHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$grabMouse,
    void
) {
    if (gUI && gUI->isVisible()) {
        if (isInGameInputEnabled())
            restoreGameMouse.store(true, std::memory_order_release);
        return;
    }
    origin();
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBlockUsePacketProbe,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer,
    void,
    Packet& packet
) {
    if (packet.getId() == MinecraftPacketIds::PlayerAction) {
        auto& action = static_cast<PlayerActionPacket&>(packet);
        if (action.mAction == PlayerActionType::InteractWithBlock ||
            action.mAction == PlayerActionType::StartItemUseOn ||
            action.mAction == PlayerActionType::StopItemUseOn) {
            const auto& pos = action.mPos.get();
            const auto& resultPos = action.mResultPos.get();
            NoteBot::getInstance().getSelf().getLogger().info(
                "BlockAction packet: action={}, pos=({},{},{}), result=({},{},{}), face={}",
                static_cast<int>(action.mAction), pos.x, pos.y, pos.z,
                resultPos.x, resultPos.y, resultPos.z, action.mFace
            );
        }
    }
    if (packet.getId() == MinecraftPacketIds::InventoryTransaction) {
        auto& inventoryPacket = static_cast<InventoryTransactionPacket&>(packet);
        if (auto* use = std::get_if<ItemUseInventoryTransaction>(&inventoryPacket.mVariantTransaction.get())) {
            if (use->mActionType == ItemUseInventoryTransaction::ActionType::Use) {
                const auto& pos = use->mPos.get();
                NoteBot::getInstance().getSelf().getLogger().info(
                    "BlockUse packet: pos=({},{},{}), face={}, slot={}, hand={}, target={}, itemCount={}, trigger={}, predicted={}",
                    pos.x, pos.y, pos.z, use->mFace, use->mSlot,
                    static_cast<int>(use->mHand), use->mTargetBlockId,
                    use->mItem.get().mStackSize, static_cast<int>(use->mTriggerType),
                    static_cast<int>(use->mClientPredictedResult)
                );
            }
        }
    }
    origin(packet);
}

const auto noteBlockEventHandler = static_cast<void (LegacyClientNetworkHandler::*)(
    NetworkIdentifier const&, BlockEventPacket const&)>(&LegacyClientNetworkHandler::$handle);

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBlockEventHook,
    ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,
    noteBlockEventHandler,
    void,
    NetworkIdentifier const& source,
    BlockEventPacket const& packet
) {
    origin(source, packet);
    static std::atomic<unsigned> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 32) {
        const auto& pos = packet.mPos.get();
        NoteBot::getInstance().getSelf().getLogger().info(
            "BlockEvent packet: pos=({},{},{}), type={}, data={}",
            pos.x, pos.y, pos.z, packet.mB0, packet.mB1
        );
    }
    NoteBlockInteraction::recordNoteEvent(packet.mPos, packet.mB1);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBotLevelExitHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$onLevelExit,
    void
) {
    NoteBlockInteraction::clearNoteEvents();
    if (gPlayer) gPlayer->requestClearNoteBlocks();
    origin();
}

} // namespace notebot
