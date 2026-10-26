// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <utility>

/// One background computation whose result is polled from the render thread.
///
/// Unlike std::future from std::async, dropping a job never waits for it: the
/// worker owns a shared copy of the state and simply finishes into the void.
/// That matters because Resolume destroys a source as soon as its clip stops,
/// possibly while a big file is still being parsed.
template< typename T >
class AsyncJob
{
public:
	template< typename Fn >
	void Start( Fn fn )
	{
		std::shared_ptr< State > s = std::make_shared< State >();
		state                      = s;
		std::thread( [ s, fn ]() mutable {
			s->value = fn();
			s->done.store( true, std::memory_order_release );
		} ).detach();
	}

	bool Running() const { return state != nullptr; }
	bool Ready() const { return state && state->done.load( std::memory_order_acquire ); }

	/// Only valid once Ready() returned true.
	T Take()
	{
		T value = std::move( state->value );
		state.reset();
		return value;
	}

	void Abandon() { state.reset(); }

private:
	struct State
	{
		std::atomic< bool > done{ false };
		T value;
	};
	std::shared_ptr< State > state;
};
