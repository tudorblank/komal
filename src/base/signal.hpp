#pragma once
// Something happened: a Signal tells every listener connected to it, in the order they
// connected. The one way anything in the app hears about a change (pixels stream through
// Node::watchOutput instead; caches compare revisions).
//
//   Signal<Project, const ProjectEvent&> changed;                        // a public member
//   Connection c = project.changed.connect([this](const ProjectEvent& e) { ... });
//   changed.emit(event);                                                 // only Project can
//
// Anyone may connect; only the owner (the first template argument) can emit.
// A listener stays connected for as long as its Connection lives: dropping the Connection (or
// the object holding it) disconnects it, so a listener never outlives what it listens to.
// Listeners may connect or disconnect while a signal is emitting: one connected meanwhile hears
// the next emit; one disconnected meanwhile isn't called again.

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace detail {
struct SignalCore {
    virtual ~SignalCore() = default;
    virtual void drop(uint64_t id) = 0;
};
}

// === CONNECTION === //
class Connection {
public:
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& o) noexcept : m_core(std::move(o.m_core)), m_id(std::exchange(o.m_id, 0)) {}
    Connection& operator=(Connection&& o) noexcept
    {
        if (this != &o) {
            disconnect();
            m_core = std::move(o.m_core);
            m_id   = std::exchange(o.m_id, 0);
        }
        return *this;
    }
    ~Connection() { disconnect(); }

    void disconnect()
    {
        if (auto core = m_core.lock()) core->drop(m_id);
        m_core.reset();
        m_id = 0;
    }

private:
    template<class, class...> friend class Signal;
    Connection(std::weak_ptr<detail::SignalCore> core, uint64_t id) : m_core(std::move(core)), m_id(id) {}

    std::weak_ptr<detail::SignalCore> m_core;
    uint64_t m_id = 0;
};

// === SIGNAL === //
template<class Owner, class... Args>
class Signal {
public:
    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    [[nodiscard]] Connection connect(std::function<void(Args...)> listener) const   // listening changes nothing
    {
        const uint64_t id = m_core->nextId++;
        m_core->slots.push_back(std::make_unique<Slot>(Slot{ id, std::move(listener), true }));
        return Connection(m_core, id);
    }

private:
    friend Owner;

    void emit(Args... args) const
    {
        const std::shared_ptr<Core> core = m_core;   // alive to the end, whatever a listener does
        ++core->emitting;
        const size_t count = core->slots.size();      // connected during this emit: the next one on
        for (size_t i = 0; i < count; ++i)
            if (Slot* s = core->slots[i].get(); s->live) s->listener(args...);
        if (--core->emitting == 0) core->sweep();
    }

    struct Slot {
        uint64_t id;
        std::function<void(Args...)> listener;
        bool live;
    };
    struct Core : detail::SignalCore {
        std::vector<std::unique_ptr<Slot>> slots;   // stable addresses: safe to grow mid-emit
        uint64_t nextId = 1;
        int emitting = 0;

        void drop(uint64_t id) override
        {
            for (auto& s : slots) if (s->id == id) s->live = false;
            if (emitting == 0) sweep();
        }
        void sweep() { std::erase_if(slots, [](const auto& s) { return !s->live; }); }
    };

    std::shared_ptr<Core> m_core = std::make_shared<Core>();
};
