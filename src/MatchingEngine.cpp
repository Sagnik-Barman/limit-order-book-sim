#include "MatchingEngine.hpp"

OrderId MatchingEngine::submit_limit(Side side, Price price, Quantity qty, bool is_market_maker) {
    OrderId id = next_id_++;
    Order order{id, side, OrderType::Limit, price, qty, clock_, is_market_maker};
    auto fills = book_.submit_limit(order);
    trade_tape_.insert(trade_tape_.end(), fills.begin(), fills.end());
    return id;
}

OrderId MatchingEngine::submit_market(Side side, Quantity qty, bool is_market_maker) {
    OrderId id = next_id_++;
    Order order{id, side, OrderType::Market, 0, qty, clock_, is_market_maker};
    auto fills = book_.submit_market(order);
    trade_tape_.insert(trade_tape_.end(), fills.begin(), fills.end());
    return id;
}

bool MatchingEngine::cancel(OrderId id) { return book_.cancel(id); }

std::vector<Fill> MatchingEngine::pop_new_fills() {
    std::vector<Fill> new_fills(trade_tape_.begin() + static_cast<long>(last_popped_index_), trade_tape_.end());
    last_popped_index_ = trade_tape_.size();
    return new_fills;
}
