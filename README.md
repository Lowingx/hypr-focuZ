# depthdeck

Reimplementação do efeito **deck por profundidade de foco** (o `hypr-focuZ` /
focusZ) em **Wayfire**, "do 0", usando só a API pública de plugins — mas
portando o algoritmo do focusZ como ele é, não uma reinvenção.

## Por que Wayfire (e não Hyprland/KWin)

| | Hyprland (focusZ) | Wayfire (depthdeck) |
|---|---|---|
| Escala de janela | resize real do cliente → reflow, flash "abre grande e encolhe" | `wf::scene::view_2d_transformer_t` — **só visual**, cliente nunca reencaminha |
| API de plugin | ABI travada, headers internos, `HyprlandAPI` | headers públicos instalados (`/usr/include/wayfire`), `wayfire.pc` |
| Blur/frost do fundo | código próprio (nunca anexado — issue #16) | plugin de base `blur` com `blur_node_t` pronto |
| Foco no hover | `follow_mouse=1` + promote → loop (85 promotes/4 s, flick) | foco é por clique por padrão; sem o loop |
| Animação/transformers | — | `simple_animation_t`, `TRANSFORMER_2D`, cadeia de transformers documentada |

## O que foi portado do focusZ (src/DepthFocus.cpp)

| focusZ | depthdeck |
|---|---|
| `rebuildStack` / `promoteWindow` | deck por output: `decks[output]` (frente primeiro) + `view_activated_state_signal` → `promote()` |
| `getTransformForLayer(depth)` | `transform_for_depth(depth)` — **idêntico**: camada 1 (0.70/0.85), camada 2 (0.50/0.70), interpolação linear até o piso `kFloorScale=0.22` / `kFloorOpacity=0.05` espalhada em `max_layers` níveis; profundidade clampada em `max_layers - 1` (índice, como o focusZ) |
| resize do cliente | `view_2d_transformer_t` (scale/alpha), dano propagado com `begin/end_transform_update` |
| `scatterOnEdges` / `scatterAroundFocused` / `clampToWorkarea` | `scatter_offset()` — **port1:1** (splitmix64, modo borda com até 64 rolagens garantindo `peek ≥ card_peek_min`, clamp na workarea) aplicado como `translation_x/y` **visual** em vez de mover o cliente |
| blur do fundo | ✅ **fase 4**: plugin `blur` de primeira-parte (matcher `blur_by_default = type is "toplevel"`, `view_matcher_t`) — toda carta translúcida revela o fundo **borrado** atrás de si = vidro fosco; a densidade por profundidade vem do alpha. Raio único global, a mesma limitação do focusZ no Hyprland ("blur follows the global `blur:size`") |
| guard de flick | *fase 5* (não crítico: Wayfire não foca no hover por padrão) |

Constantes do focusZ preservadas em `src/depthdeck.cpp`:
`kFloorScale = 0.22f`, `kFloorOpacity = 0.05f`, `kEdgeInset = 16.0`.

## Fases

1. ✅ **Esqueleto**: deck + escala/opacidade por profundidade.
2. ✅ **Scatter de posição** (port do focusZ: borda/ao-redor, faixa de peek, reshuffle por nonce, clamp na workarea) via `translation_x/y` do transformer.
3. Animação (`wf::animation::simple_animation_t`) para escala/alpha/translação.
4. ✅ **Blur/frost do fundo** — plugin `blur` na lista de plugins (matcher padrão casa todo toplevel); densidade por profundidade via alpha. Nesta fase também: **todas** as opções do depthdeck ganharam `set_callback` → relayout em runtime (WCM/`wayfire/set-config-options`), sem esperar o próximo promote/unmap.
5. Guard de flick + atalhos de cycling + integração WCM.
6. Paridade fina com focusZ (janelas maximizadas, workarea reservada, reshuffle fino).

## Build & run

```sh
sudo pacman -S wayfire        # uma vez; traz headers + wayfire.pc
./scripts/dev-run.sh          # compila, instala em ~/.local e abre aninhado
```

O script:

1. `meson setup/compile` (o plugin compila **sem root**);
2. instala `libdepthdeck.so` em `~/.local/share/wayfire/plugins/`
   (caminho procurado pelo loader) e o schema em
   `~/.local/share/wayfire/metadata/depthdeck.xml` (obrigatório: sem ele o
   `option_wrapper` lança `No such option` e o plugin não inicia);
3. roda `wayfire --config wayfire.ini` **aninhado dentro da sessão atual** —
   nada da sessão host é tocado; feche a janela para voltar.

Teste: abra 2–3 janelas dentro do Wayfire aninhado e alterne o foco
(clique). A da frente fica em escala 1.0, as de trás encolhem/diminuem pela
curva.

## Layout do repositório

```
meson.build              dependência wayfire (>=0.11)
src/depthdeck.cpp        o plugin (deck + curva de profundidade)
metadata/depthdeck.xml   schema de opções (wf-config / WCM)
wayfire.ini              config do teste aninhado ([depthdeck] no fim)
scripts/dev-run.sh       compila + instala + roda aninhado
```
