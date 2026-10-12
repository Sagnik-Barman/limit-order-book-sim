#include "OrderBook.hpp"

#include <algorithm>

namespace {
bool crosses(Side incoming_side, Price incoming_price, Price resting_price) {
    return incoming_side == Side::Buy ? incoming_price >= resting_price
                                       : incoming_price <= resting_price;
}
}  // namespace

std::vector<Fill> OrderBook::match(Order &incoming) {
    std::vector<Fill> fills;
    auto &opposite_bids = bids_;
    auto &opposite_asks = asks_;

    if (incoming.side == Side::Buy) {
        while (incoming.quantity > 0 && !opposite_asks.empty()) {
            auto level_it = opposite_asks.begin();
            Price level_price = level_it->first;
            if (incoming.type == OrderType::Limit && !crosses(incoming.side, incoming.price, level_price)) {
                break;
            }
            Level &level = level_it->second;
            while (incoming.quantity > 0 && !level.empty()) {
                Order &resting = level.front();
                Quantity traded = std::min(incoming.quantity, resting.quantity);

                fills.push_back(Fill{incoming.timestamp, level_price, traded, incoming.id, resting.id,
                                      incoming.side, incoming.is_market_maker, resting.is_market_maker});

                incoming.quantity -= traded;
                resting.quantity -= traded;
                if (resting.quantity == 0) {
                    asks_by_id_.erase(resting.id);
                    level.pop_front();
                }
            }
            if (level.empty()) opposite_asks.erase(level_it);
        }
    } else {
        while (incoming.quantity > 0 && !opposite_bids.empty()) {
            auto level_it = opposite_bids.begin();
            Price level_price = level_it->first;
            if (incoming.type == OrderType::Limit && !crosses(incoming.side, incoming.price, level_price)) {
                break;
            }
            Level &level = level_it->second;
            while (incoming.quantity > 0 && !level.empty()) {
                Order &resting = level.front();
                Quantity traded = std::min(incoming.quantity, resting.quantity);

                fills.push_back(Fill{incoming.timestamp, level_price, traded, incoming.id, resting.id,
                                      incoming.side, incoming.is_market_maker, resting.is_market_maker});

                incoming.quantity -= traded;
                resting.quantity -= traded;
                if (resting.quantity == 0) {
                    bids_by_id_.erase(resting.id);
                    level.pop_front();
                }
            }
            if (level.empty()) opposite_bids.erase(level_it);
        }
    }
    return fills;
}

void OrderBook::rest(Order order) {
    if (order.side == Side::Buy) {
        auto lvl = bids_.try_emplace(order.price).first;
        lvl->second.push_back(order);
        bids_by_id_[order.id] = {lvl, std::prev(lvl->second.end())};
    } else {
        auto lvl = asks_.try_emplace(order.price).first;
        lvl->second.push_back(order);
        asks_by_id_[order.id] = {lvl, std::prev(lvl->second.end())};
    }
}

std::vector<Fill> OrderBook::submit_limit(Order order) {
    order.type = OrderType::Limit;
    auto fills = match(order);
    if (order.quantity > 0) rest(order);
    return fills;
}

std::vector<Fill> OrderBook::submit_market(Order order) {
    order.type = OrderType::Market;
    // remaining quantity that can't be filled (book exhausted) is simply
    // dropped -- a real exchange would reject/partially-fill too.
    return match(order);
}

void OrderBook::erase_from_index(OrderId id, Side side) {
    auto &index = (side == Side::Buy) ? bids_by_id_ : asks_by_id_;
    index.erase(id);
}

bool OrderBook::cancel(OrderId id) {
    if (auto it = bids_by_id_.find(id); it != bids_by_id_.end()) {
        auto [level_it, list_it] = it->second;   // O(1): no map lookup
        level_it->second.erase(list_it);
        if (level_it->second.empty()) bids_.erase(level_it);  // amortized O(1)
        bids_by_id_.erase(it);
        return true;
    }
    if (auto it = asks_by_id_.find(id); it != asks_by_id_.end()) {
        auto [level_it, list_it] = it->second;   // O(1): no map lookup
        level_it->second.erase(list_it);
        if (level_it->second.empty()) asks_.erase(level_it);  // amortized O(1)
        asks_by_id_.erase(it);
        return true;
    }
    return false;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::quantity_at_bid(Price p) const {
    auto it = bids_.find(p);
    if (it == bids_.end()) return 0;
    Quantity total = 0;
    for (const auto &o : it->second) total += o.quantity;
    return total;
}

Quantity OrderBook::quantity_at_ask(Price p) const {
    auto it = asks_.find(p);
    if (it == asks_.end()) return 0;
    Quantity total = 0;
    for (const auto &o : it->second) total += o.quantity;
    return total;
}

std::optional<double> OrderBook::mid_price_ticks() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return (static_cast<double>(*bb) + static_cast<double>(*ba)) / 2.0;
}

std::optional<Price> OrderBook::spread_ticks() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return *ba - *bb;
}

std::vector<std::pair<Price, Quantity>> OrderBook::bid_levels(size_t depth) const {
    std::vector<std::pair<Price, Quantity>> out;
    size_t n = 0;
    for (const auto &[price, level] : bids_) {
        if (n++ >= depth) break;
        Quantity total = 0;
        for (const auto &o : level) total += o.quantity;
        out.emplace_back(price, total);
    }
    return out;
}

std::vector<std::pair<Price, Quantity>> OrderBook::ask_levels(size_t depth) const {
    std::vector<std::pair<Price, Quantity>> out;
    size_t n = 0;
    for (const auto &[price, level] : asks_) {
        if (n++ >= depth) break;
        Quantity total = 0;
        for (const auto &o : level) total += o.quantity;
        out.emplace_back(price, total);
    }
    return out;
}
