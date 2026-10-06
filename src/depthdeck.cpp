/*
 * depthdeck — depth-focus deck for Wayfire.
 *
 * Port of the hypr-focuZ (focusZ) depth engine onto Wayfire's public plugin
 * API:
 *
 *   - deck order per output: the focused card goes to the front (promote),
 *     new cards enter at the front, unmapped cards leave the deck;
 *   - depth transform: layer 1 / layer 2 scale+opacity, then a linear
 *     interpolation down to the 0.22/0.05 floor spread over max_layers
 *     levels (ported from CDepthFocusManager::getTransformForLayer);
 *   - back-card scatter: cards behind the front are offset towards a random
 *     workarea edge (or around the focused card) so each one keeps a peek
 *     strip of at least card_peek_min px visible — ported from
 *     CDepthFocusManager::scatterOnEdges/scatterAroundFocused. Where focusZ
 *     moved the client box (setPositionGlobal on floating windows), here the
 *     offset is a visual translation on the transformer, so the client is
 *     never repositioned either;
 *
 * The architectural difference that fixes focusZ's bugs: scale and opacity are
 * applied *visually* through wf::scene::view_2d_transformer_t. The client is
 * never resized, so there is no reflow, no "map big, then shrink" flash and no
 * dependence on the geometry a window happened to map with. Wayfire also
 * doesn't focus on hover by default, which removes the promote-on-hover loop
 * that made focusZ flicker (85 promotes / 4 s with follow_mouse=1).
 */

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/workarea.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

static constexpr const char *TRANSFORMER_NAME = "depthdeck";

// focusz::constants, from hypr-focuZ/src/DepthFocus.cpp
static constexpr float kFloorScale   = 0.22f;
static constexpr float kFloorOpacity = 0.05f;
static constexpr double kEdgeInset   = 16.0;
static constexpr double kTwoPi       = 6.283185307179586;

struct depth_transform_t
{
    float scale;
    float opacity;
    // Visual scatter offset in output pixels (0 for the front card).
    float tx = 0.0f;
    float ty = 0.0f;
};

class depthdeck_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        on_view_mapped = [this] (wf::view_mapped_signal *ev)
        {
            track(wf::toplevel_cast(ev->view));
        };
        wf::get_core().connect(&on_view_mapped);

        on_view_unmapped = [this] (wf::view_unmapped_signal *ev)
        {
            untrack(wf::toplevel_cast(ev->view));
        };
        wf::get_core().connect(&on_view_unmapped);

        // set_activated() is emitted on the view itself only, so this one
        // shared connection is registered on every tracked view.
        on_view_activated = [this] (wf::view_activated_state_signal *ev)
        {
            if (ev->view && ev->view->activated)
            {
                promote(ev->view.get());
            }
        };

        on_output_added = [this] (wf::output_added_signal *ev)
        {
            seed(ev->output);
        };
        wf::get_core().output_layout->connect(&on_output_added);

        on_output_removed = [this] (wf::output_removed_signal *ev)
        {
            decks.erase(ev->output);
            for (auto& [ptr, card] : cards)
            {
                if (card->output == ev->output)
                {
                    card->output = nullptr;
                }
            }
        };
        wf::get_core().output_layout->connect(&on_output_removed);

        enabled.set_callback([this] { refresh_all(); });

        for (auto output : wf::get_core().output_layout->get_outputs())
        {
            seed(output);
        }

        LOGI("depthdeck: initialized");
    }

    void fini() override
    {
        on_view_mapped.disconnect();
        on_view_unmapped.disconnect();
        on_output_added.disconnect();
        on_output_removed.disconnect();
        enabled.set_callback(nullptr);

        for (auto& [ptr, card] : cards)
        {
            detach(card.get());
        }

        cards.clear();
        decks.clear();
        LOGI("depthdeck: finalized");
    }

  private:
    /* ---------- state ------------------------------------------------- */

    struct card_t
    {
        wayfire_toplevel_view view = nullptr;
        wf::output_t *output = nullptr;
        std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    };

    // Keyed by raw pointer: a card never keeps a view alive.
    std::map<wf::toplevel_view_interface_t *, std::unique_ptr<card_t>> cards;
    // Per-output deck, front (depth 0) first.
    std::map<wf::output_t *, std::vector<wf::toplevel_view_interface_t *>> decks;

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped;
    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped;
    wf::signal::connection_t<wf::view_activated_state_signal> on_view_activated;
    wf::signal::connection_t<wf::output_added_signal> on_output_added;
    wf::signal::connection_t<wf::output_removed_signal> on_output_removed;

    wf::option_wrapper_t<bool> enabled{"depthdeck/enabled"};
    wf::option_wrapper_t<double> layer1_scale{"depthdeck/layer_1_scale"};
    wf::option_wrapper_t<double> layer1_opacity{"depthdeck/layer_1_opacity"};
    wf::option_wrapper_t<double> layer2_scale{"depthdeck/layer_2_scale"};
    wf::option_wrapper_t<double> layer2_opacity{"depthdeck/layer_2_opacity"};
    wf::option_wrapper_t<int> max_layers{"depthdeck/max_layers"};
    wf::option_wrapper_t<bool> scatter{"depthdeck/scatter"};
    wf::option_wrapper_t<bool> card_edge_scatter{"depthdeck/card_edge_scatter"};
    wf::option_wrapper_t<bool> card_scatter_reshuffle{"depthdeck/card_scatter_reshuffle"};
    wf::option_wrapper_t<double> card_peek_min{"depthdeck/card_peek_min"};
    wf::option_wrapper_t<double> card_peek_max{"depthdeck/card_peek_max"};

    // focusZ's m_dealNonce: bumped whenever the deck membership changes so a
    // reshuffle gives the back cards fresh positions instead of the same roll.
    uint64_t deal_nonce = 0;

    /* ---------- deck bookkeeping -------------------------------------- */

    void track(wayfire_toplevel_view view)
    {
        if (!view || cards.count(view.get()))
        {
            return;
        }

        auto card         = std::make_unique<card_t>();
        card->view        = view;
        card->output      = view->get_output();
        cards[view.get()] = std::move(card);

        view->connect(&on_view_activated);

        // New cards enter at the front; focus history sorts it out from there.
        place_front(view.get());
        deal_nonce++;
    }

    void untrack(wayfire_toplevel_view view)
    {
        auto it = cards.find(view.get());
        if (it == cards.end())
        {
            return;
        }

        wf::output_t *output = it->second->output;
        view->disconnect(&on_view_activated);
        detach(it->second.get());
        remove_from_deck(output, view.get());
        cards.erase(it);
        deal_nonce++;
        layout(output);
    }

    void promote(wf::toplevel_view_interface_t *view)
    {
        auto it = cards.find(view);
        if (it == cards.end())
        {
            return;
        }

        wf::output_t *output = view->get_output();
        if (!output)
        {
            return; // no deck to order until the view lands on an output
        }

        if (it->second->output != output)
        {
            remove_from_deck(it->second->output, view);
            it->second->output = output;
        }

        place_front(view);
        layout(output);
    }

    void place_front(wf::toplevel_view_interface_t *view)
    {
        auto it = cards.find(view);
        if ((it == cards.end()) || !it->second->output)
        {
            return;
        }

        auto& deck = decks[it->second->output];
        deck.erase(std::remove(deck.begin(), deck.end(), view), deck.end());
        deck.insert(deck.begin(), view);
    }

    void remove_from_deck(wf::output_t *output, wf::toplevel_view_interface_t *view)
    {
        auto it = decks.find(output);
        if (it == decks.end())
        {
            return;
        }

        auto& deck = it->second;
        deck.erase(std::remove(deck.begin(), deck.end(), view), deck.end());
    }

    /* ---------- layout ------------------------------------------------ */

    void seed(wf::output_t *output)
    {
        if (!output)
        {
            return;
        }

        auto wset = output->wset();
        if (!wset)
        {
            return;
        }

        // track() pushes to the front, so seeding in map order leaves the
        // newest card at depth 0.
        wayfire_toplevel_view activated = nullptr;
        for (auto& view : wset->get_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                continue;
            }

            track(toplevel);
            if (toplevel->activated)
            {
                activated = toplevel;
            }
        }

        if (activated)
        {
            promote(activated.get());
        } else
        {
            layout(output);
        }
    }

    void layout(wf::output_t *output)
    {
        if (!output)
        {
            return;
        }

        auto deck_it = decks.find(output);
        if (deck_it == decks.end())
        {
            return;
        }

        auto& deck = deck_it->second;

        // Drop stale entries (views that changed output or vanished).
        deck.erase(std::remove_if(deck.begin(), deck.end(), [&] (auto *view)
        {
            auto it = cards.find(view);
            return (it == cards.end()) || (it->second->output != output);
        }), deck.end());

        if (deck.empty())
        {
            return;
        }

        if (!enabled)
        {
            for (auto *view : deck)
            {
                detach(cards[view].get());
            }

            return;
        }

        // focusZ clamps depth to maxLayers - 1 (an index, not a count): with
        // max_layers = 8 the deepest card is depth 7.
        const int max_depth = std::max<int>(1, max_layers) - 1;

        // The front card's box anchors the scatter: back cards are positioned
        // relative to it, exactly like focusZ's frontBox.
        wf::geometry_t front_box;
        bool            have_front = false;
        auto front_it = cards.find(deck.front());
        if ((front_it != cards.end()) && front_it->second->view)
        {
            front_box   = front_it->second->view->get_geometry();
            have_front  = true;
        }

        for (size_t i = 0; i < deck.size(); i++)
        {
            auto it = cards.find(deck[i]);
            if (it == cards.end())
            {
                continue;
            }

            // Depth is clamped to max_layers: everything deeper shares the
            // deepest treatment (the transform floors at 0.22/0.05 anyway).
            auto t    = transform_for_depth(std::min<int>(i, max_depth));
            t.tx      = 0.0f;
            t.ty      = 0.0f;

            if (scatter && have_front && (i > 0))
            {
                auto off = scatter_offset(output, it->second.get(), front_box,
                    (int)i, t.scale);
                t.tx = off.x;
                t.ty = off.y;
            }

            apply(it->second.get(), t);
        }
    }

    void refresh_all()
    {
        for (auto& [output, deck] : decks)
        {
            layout(output);
        }
    }

    /* ---------- depth curve ------------------------------------------- */

    depth_transform_t transform_for_depth(int depth) const
    {
        if (depth <= 0)
        {
            return {1.0f, 1.0f};
        }

        if (depth == 1)
        {
            return {(float)layer1_scale, (float)layer1_opacity};
        }

        if (depth == 2)
        {
            return {(float)layer2_scale, (float)layer2_opacity};
        }

        // Deeper layers interpolate from layer 2 down to the floor across the
        // remaining configured levels — otherwise every window past depth 3
        // would look identical (the "dim only happens once" bug focusZ fixed).
        const float base_scale   = layer2_scale;
        const float base_opacity = layer2_opacity;
        const int   total_steps  = std::max<int>(1, (int)max_layers - 2);
        const float frac         = std::min(1.0f, (float)(depth - 2) / (float)total_steps);

        return {
            base_scale + (kFloorScale - base_scale) * frac,
            base_opacity + (kFloorOpacity - base_opacity) * frac,
        };
    }

    /* ---------- back-card scatter -------------------------------------- */

    // Deterministic splitmix64 finalizer — focusZ's randSeed(), so the same
    // (view, depth, nonce) always rolls the same position.
    static uint64_t rand_seed(uint64_t seed)
    {
        uint64_t h = seed * 0x9E3779B97F4A7C15ull;
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return h;
    }

    // Visual scatter offset (output px) for a back card: where focusZ moved
    // the client box, we compute the same target box and return the delta
    // between its center and the card's natural center — the transformer
    // applies it after scaling, so the math composes unchanged.
    wf::pointf_t scatter_offset(wf::output_t *output, card_t *card,
        const wf::geometry_t& front, int depth, float scale) const
    {
        const wf::geometry_t g    = card->view->get_geometry();
        const double         cw   = g.width * scale;
        const double         ch   = g.height * scale;
        const wf::geometry_t work = output->workarea->get_workarea();

        const uint64_t  nonce_mix =
            card_scatter_reshuffle ? (uint64_t)deal_nonce * 0x9E3779B97F4A7C15ull : 0ull;
        const uint64_t  addr = (uint64_t)(uintptr_t)card->view.get();
        const double    peek_min = card_peek_min;
        const double    peek_max = card_peek_max;

        double tx = work.x;
        double ty = work.y;

        if (card_edge_scatter)
        {
            // Edge-bias mode: up to 64 rolls, keeping the position with the
            // biggest peek beyond the front box and stopping as soon as the
            // card sticks out by at least peek_min px.
            double best_peek = -1.0;
            for (int attempt = 0; attempt < 64; attempt++)
            {
                const uint64_t r     = rand_seed(addr ^ ((uint64_t)depth << 32) ^ nonce_mix ^
                                                 ((uint64_t)attempt * 0x9E3779B97F4A7C15ull));
                const int      edge  = (int)(r >> 0) & 0x3;
                const double   along = (double)((r >> 16) & 0xFFFF) / 65536.0;
                double x, y;
                switch (edge)
                {
                    case 0: // top
                        x = work.x + along * std::max(0.0, (double)work.width - cw);
                        y = work.y + kEdgeInset;
                        break;
                    case 1: // right
                        x = work.x + work.width - cw - kEdgeInset;
                        y = work.y + along * std::max(0.0, (double)work.height - ch);
                        break;
                    case 2: // bottom
                        x = work.x + along * std::max(0.0, (double)work.width - cw);
                        y = work.y + work.height - ch - kEdgeInset;
                        break;
                    default: // left
                        x = work.x + kEdgeInset;
                        y = work.y + along * std::max(0.0, (double)work.height - ch);
                        break;
                }

                const double peek = std::max(
                    std::max(front.x - x, x + cw - (front.x + front.width)),
                    std::max(front.y - y, y + ch - (front.y + front.height)));

                if (peek > best_peek)
                {
                    best_peek = peek;
                    tx = x;
                    ty = y;
                }

                if (peek >= peek_min)
                {
                    break;
                }
            }
        } else
        {
            // Around-focused mode: a random angle and a radius of peek_min..
            // peek_max beyond the front card's edge.
            const uint64_t r      = rand_seed(addr ^ ((uint64_t)depth << 32) ^ nonce_mix);
            const double   angle  = (double)(r & 0xFFFF) / 65536.0 * kTwoPi;
            const double   radius = peek_min +
                                   (double)((r >> 48) & 0xFFFF) / 65536.0 * (peek_max - peek_min);
            const double   fcx    = front.x + front.width / 2.0;
            const double   fcy    = front.y + front.height / 2.0;
            const double   cx     = fcx + std::cos(angle) * (front.width / 2.0 + radius);
            const double   cy     = fcy + std::sin(angle) * (front.height / 2.0 + radius);
            tx = cx - cw / 2.0;
            ty = cy - ch / 2.0;
        }

        // Keep the card inside the workarea (focusZ's clampToWorkarea).
        if (tx < work.x)
        {
            tx = work.x;
        }

        if (tx + cw > work.x + work.width)
        {
            tx = work.x + work.width - cw;
        }

        if (ty < work.y)
        {
            ty = work.y;
        }

        if (ty + ch > work.y + work.height)
        {
            ty = work.y + work.height - ch;
        }

        const double gcx = g.x + g.width / 2.0;
        const double gcy = g.y + g.height / 2.0;
        return {(float)((tx + cw / 2.0) - gcx), (float)((ty + ch / 2.0) - gcy)};
    }

    /* ---------- transform application ---------------------------------- */

    void apply(card_t *card, depth_transform_t t)
    {
        if (!card || !card->view)
        {
            return;
        }

        auto& node = card->view->get_transformed_node();
        if (!card->transformer)
        {
            card->transformer = std::make_shared<wf::scene::view_2d_transformer_t>(card->view);
            node->add_transformer(card->transformer, wf::TRANSFORMER_2D + 1, TRANSFORMER_NAME);
        }

        auto& tr = *card->transformer;
        if ((tr.scale_x == t.scale) && (tr.alpha == t.opacity) &&
            (tr.translation_x == t.tx) && (tr.translation_y == t.ty))
        {
            return;
        }

        node->begin_transform_update();
        tr.scale_x = tr.scale_y = t.scale;
        tr.alpha   = t.opacity;
        // Post-scale, in output pixels — the card's visual center lands on
        // natural center + (tx, ty).
        tr.translation_x = t.tx;
        tr.translation_y = t.ty;
        node->end_transform_update();
    }

    void detach(card_t *card)
    {
        if (!card || !card->view || !card->transformer)
        {
            return;
        }

        card->view->get_transformed_node()->rem_transformer(TRANSFORMER_NAME);
        card->transformer.reset();
    }
};

DECLARE_WAYFIRE_PLUGIN(depthdeck_t);
