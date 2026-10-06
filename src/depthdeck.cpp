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

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

static constexpr const char *TRANSFORMER_NAME = "depthdeck";

// focusz::constants, from hypr-focuZ/src/DepthFocus.cpp
static constexpr float kFloorScale   = 0.22f;
static constexpr float kFloorOpacity = 0.05f;

struct depth_transform_t
{
    float scale;
    float opacity;
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

        if (!enabled)
        {
            for (auto *view : deck)
            {
                detach(cards[view].get());
            }

            return;
        }

        const int levels = std::max<int>(1, max_layers);
        for (size_t i = 0; i < deck.size(); i++)
        {
            auto it = cards.find(deck[i]);
            if (it == cards.end())
            {
                continue;
            }

            // Depth is clamped to max_layers: everything deeper shares the
            // deepest treatment (the transform floors at 0.22/0.05 anyway).
            apply(it->second.get(), transform_for_depth(std::min<int>(i, levels)));
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
        if ((tr.scale_x == t.scale) && (tr.alpha == t.opacity))
        {
            return;
        }

        node->begin_transform_update();
        tr.scale_x = tr.scale_y = t.scale;
        tr.alpha   = t.opacity;
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
