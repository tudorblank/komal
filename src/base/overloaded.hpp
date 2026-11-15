#pragma once
// One lambda per alternative, for std::visit: every alternative must have one, or it doesn't
// compile. The way to handle a variant whole (a drag, a control, a node's settings).
//
//   std::visit(Overloaded{ [](const A& a) { ... }, [](const B& b) { ... } }, v);

template <class... F> struct Overloaded : F... { using F::operator()...; };
