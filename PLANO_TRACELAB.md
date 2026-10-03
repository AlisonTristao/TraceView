# Plano: TraceLab

Criado: 2026-10-02
Repositórios: TraceView (pasta `lab/`), BTP, BTPDevice, bally_OS
Estado: **consolidação do que foi discutido**. Nenhum código do TraceLab foi
escrito. Complementa o [PLANO_PERFIS_E_MALHA.md](PLANO_PERFIS_E_MALHA.md), que
trata da comunicação.

---

## 1. O que é o TraceLab

O TraceLab é o modo de **simulação e controle** do TraceView. Com ele você
descreve uma planta por equações de estado, simula com um solver reconhecido,
fecha a malha com um controlador que você programa e vê tudo nos dashboards e
numa cena animada.

O ponto central: **a planta e o controlador são endpoints BTP** que falam o
perfil `loop`. Cada um pode rodar onde fizer sentido, e trocar um de lugar não
muda o outro:

| Estágio | Planta | Controlador | O que muda ao passar para o próximo |
|---|---|---|---|
| **1. Simulação completa** | planta simulada (TraceLab) | TraceLab | — |
| **2. Robô real, controle no PC** | robô real | TraceLab | só o dispositivo da planta |
| **3. Tudo embarcado** | robô real | firmware | o controlador é compilado no firmware |
| HIL (depois) | planta simulada | firmware | plano próprio, usa a mesma língua |

O TraceLab é genérico. O seguidor de linha é o primeiro caso, e o pêndulo de
Furuta e outros benchmarks vêm depois, sem mudar a estrutura.

---

## 2. Decisões

| # | Decisão | Motivo |
|---|---|---|
| L1 | **O TraceLab é um módulo do TraceView** (pasta `lab/`), não um app separado. Ele é ligado em Configurações → Módulos, e um segundo atalho "TraceLab" abre o mesmo executável com `--mode lab` e a identidade do TraceLab | Uma instalação, um updater, um pipeline. Quem só usa telemetria não vê nada |
| L2 | **Só desktop.** A opção `TRACEVIEW_ENABLE_LAB` fica desligada em Android e iOS | Simulação no celular não vale o custo. Libera C++ puro e bibliotecas de desktop |
| L3 | **Regra de fronteira:** o que for útil para outro hardware vai para `lib/` (TraceView). Simulação, malha e benchmarks vão para `lab/`. A dependência vai só de `lab/` para `lib/` | Mantém o TraceView como ferramenta de telemetria |
| L4 | **BTP em tudo**, inclusive quando planta e controlador estão no mesmo processo. Não há atalho por chamada direta por enquanto | Um caminho só, o mesmo do robô. A 80 Hz o custo de serializar é desprezível. A ligação direta pode entrar depois por trás do mesmo contrato, se um lote ficar lento |
| L5 | **O controlador só vê o que o robô real veria.** A verdade da simulação vai para `sim.truth`, usado só para avaliação. Informação privilegiada (pontos futuros da pista) só existe num tópico explícito `sim.preview` | Evita que um controlador dependa, sem você perceber, de algo que o robô não tem |
| L6 | **Modelo em arquivo `.tlmodel` (JSON)**, com as equações em texto compiladas pela **ExprTk** | Escrever a dinâmica sem recompilar. A ExprTk é MIT, de um header só, rápida e aceita funções próprias |
| L7 | **Solver: SUNDIALS** (CVODE e ARKODE, BSD-3), mais Euler e RK4 de passo fixo | Referência em integração numérica, o que dá credibilidade. O passo fixo reproduz o firmware e o simulador Python |
| L8 | **O controlador é programável e modular.** Há três formas: DLL `loopctl` (C/C++), JavaScript (só para controladores rápidos) e processo externo (Python ou outro) via BTP | Nenhum controle fixo. O mesmo código C compila no firmware |
| L9 | **MPC:** primeiro o seu MPC Python como processo externo, depois uma DLL `loopctl` com **OSQP**. Se um dia houver NMPC, **acados** | Fecha a malha sem portar nada e depois deixa o controlador rápido e embarcável |
| L10 | **Cenas animadas em C++**, uma classe por benchmark, com registro de tipos. Sem JavaScript na animação | Desempenho, tipos checados e testes. O `.tvproj` não carrega código, então compartilhar projeto é seguro |
| L11 | **O `.tvproj` guarda tudo o que é preciso para reabrir em outra máquina:** tipo da cena, ligações e parâmetros, além do modelo e da configuração da simulação | Mandar o arquivo para alguém e funcionar igual |

---

## 3. Arquitetura

```
              ┌───────────────────── TraceLab (lab/) ─────────────────────┐
              │                                                           │
 .tlmodel ──► │  compilador (ExprTk) ─► f(t,x,u,p), g(t,x,u,p)           │
              │            │                                              │
              │  agendador (tick, ZOH) ─► SUNDIALS ─► saídas              │
              │            │                                              │
              │  ┌─────────▼──────────────┐      ┌────────────────────┐  │
              │  │ planta simulada        │ BTP  │ host de controlador │  │
              │  │ (dispositivo BTP)      │◄────►│ (dispositivo BTP)   │  │
              │  │ loop.state, sim.*      │loop.u│ DLL │ JS │ externo  │  │
              │  └─────────┬──────────────┘      └─────────┬──────────┘  │
              └────────────┼───────────────────────────────┼─────────────┘
                           │ telemetria                    │ ctl.diag
                           ▼                               ▼
                 dashboards do TraceView  +  widget Cena (C++)
```

Cada seta é BTP de verdade. O transporte interno é um loopback dentro do
processo. Qualquer endpoint também pode ser exposto por TCP, para ligar o
`tracelab-cli`, outro programa, outro PC ou, mais adiante, o robô no HIL.

---

## 4. A planta simulada

### 4.1 O modelo (`.tlmodel`)

```json
{
  "format": "tlmodel/1",
  "name": "Diferencial com motores de 1ª ordem",
  "time": "continuous",
  "parameters": { "r": 0.04, "L": 0.15, "tau_l": 0.124, "tau_r": 0.116,
                  "k_l": 1.0, "k_r": 1.0, "wmax": 104.7 },
  "inputs":  [ { "name": "u_l", "unit": "%", "min": -100, "max": 100 },
               { "name": "u_r", "unit": "%", "min": -100, "max": 100 } ],
  "states":  [ { "name": "w_l", "unit": "rad/s", "x0": 0 },
               { "name": "w_r", "unit": "rad/s", "x0": 0 },
               { "name": "x",   "unit": "m" },
               { "name": "y",   "unit": "m" },
               { "name": "th",  "unit": "rad" } ],
  "equations": {
    "w_l": "(k_l*wmax*u_l/100 - w_l)/tau_l",
    "w_r": "(k_r*wmax*u_r/100 - w_r)/tau_r",
    "x":   "r*(w_l+w_r)/2*cos(th)",
    "y":   "r*(w_l+w_r)/2*sin(th)",
    "th":  "r*(w_l-w_r)/L"
  },
  "outputs": {
    "s":   "track.sensors(x, y, th)",
    "w_l": "w_l",
    "w_r": "w_r"
  },
  "scene": { "type": "diffdrive", "params": { "body_m": 0.16 } }
}
```

- **Contínuo ou discreto.** `"time": "discrete"` aceita `x[k+1] = f(x[k], u[k])`,
  o que reproduz exatamente o motor do simulador Python.
- **Ambientes.** O que não cabe numa equação, como a geometria da pista e a
  leitura dos sensores, é um bloco nativo em C++ que expõe funções às
  expressões (`track.sensors(...)`). O núcleo continua genérico.
- **Biblioteca de funções:** `sat()`, `deadzone()`, `quantize()`, `noise()` e
  atraso.
- **A planta sugere a cena** pelo campo `scene`.

### 4.2 Simulação

- **Dois relógios separados:**

  | | O que é | Exemplo |
  |---|---|---|
  | **Período de amostragem** | tick da malha: a planta publica `loop.state` e aplica `loop.u` | 12,5 ms (80 Hz) |
  | **Passo do solver** | passo fixo, ou tolerâncias `rtol`/`atol` no adaptativo | 1 ms RK4, ou BDF com 1e-6 |

  Entre dois ticks a entrada fica constante (ZOH), como num sistema amostrado real.
- **Solvers:**
  - CVODE: Adams e BDF, com passo e ordem variáveis e **detecção de eventos**
    ("saiu da pista", "completou a volta");
  - ARKODE: Runge–Kutta explícito e implícito, com passo fixo ou adaptativo;
  - Euler e RK4 de passo fixo. Se não saírem das tabelas do ARKODE, viram
    implementação própria, validada contra ele.
- **Modos de execução:**
  - **tempo real**, 1× ou com escala;
  - **o mais rápido possível**, para lote e comparação de controladores;
  - **lockstep**: a planta espera o `loop.u` antes de avançar. É determinístico,
    independente da velocidade do controlador.
- **Rede simulada.** O transporte interno pode injetar atraso e perda, para testar
  o controlador contra o link antes do robô.

### 4.3 A planta como dispositivo BTP

O manifest é gerado a partir do `.tlmodel`, sem declarar nada à mão:

| Do modelo | No BTP |
|---|---|
| saídas | campos de `loop.state` (telemetria, a cada tick, com `tick`) |
| entradas, com min/max | parâmetros da ação STREAM `loop.u` (`ref_tick` + horizonte) |
| estados | tópico `sim.states` |
| parâmetros | ação `loop.config`, para ajustar ao vivo, e tópico `sim.params` |
| verdade da simulação | tópico `sim.truth`, só para avaliação |
| informação privilegiada (opcional) | tópico `sim.preview`, explícito |
| geometria do ambiente | tópico de documento `sim.track` |
| controle da simulação | ações `sim.run`, `sim.pause`, `sim.reset`, `sim.step`, `sim.disturb` |
| modo da malha | ação `loop.mode` (`DANGEROUS`, com `arm`) |
| aplicado de fato | tópico `loop.output` |

Na aba Dispositivos, a planta aparece como um dispositivo, por exemplo
"TraceLab: Diferencial com motores…". A ligação por nome e o "+ Adicionar
todos os campos" já funcionam com ela.

---

## 5. O controlador

### 5.1 Host de controlador

O host tem um slot "controlador" e também é um dispositivo BTP. Ele:
- recebe `loop.state` e roda o controlador **a cada amostra que chega**, sem
  timer próprio;
- envia `loop.u` com o horizonte inteiro (M = 1 para um controlador simples);
- traduz nomes em posições: as medições de `loop.state` viram `y[]` e as
  entradas de `loop.u` viram `u[]`, na ordem declarada;
- publica `ctl.diag`: tempo de solução, iterações, referência e trajetória
  prevista. Isso substitui os gráficos "ref vs pred" do simulador Python;
- deixa editar os parâmetros do controlador ao vivo, com nome, unidade e faixa.

### 5.2 Três formas de programar

| Forma | Para quê | Detalhes |
|---|---|---|
| **DLL `loopctl`** (C/C++) | controladores de verdade, MPC | Interface `btp/profiles/loopctl.h`. É recarregada ao recompilar. O mesmo código-fonte compila no firmware (estágio 3). Template CMake com OSQP via `FetchContent` |
| **JavaScript** | controladores rápidos: PD, máquina de estados, ajuste de ganho | Usa o motor JS que o TraceView já tem. Não serve para otimização pesada |
| **Processo externo** | Python, MATLAB, Julia… | Fala BTP por TCP. É por aqui que o seu MPC Python atual roda sem porte |

### 5.3 MPC e otimizador

1. **Primeiro: o MPC Python como processo externo.** Ele se conecta à planta por
   BTP/TCP, recebe `loop.state`, roda o `qlpv_mpc.py` e devolve `loop.u` com o
   plano inteiro. Para isso é preciso um **cliente BTP mínimo em Python**, que
   pode partir da codificação Python que já existe nas ferramentas de vetores
   do BTP.
2. **Depois: uma DLL `loopctl` com OSQP.** O solver é dependência do
   controlador, não do TraceLab. O OSQP tem warm start e modo embarcado, o que
   ajuda no estágio 3. Para horizontes longos há o HPIPM, e para NMPC o acados.
3. **Tempo do otimizador:**
   - em **lockstep**, o resultado independe da velocidade do solver (bom para o
     algoritmo);
   - em **tempo real**, um pacote atrasado aparece em `loop.stats` (bom para saber
     se o MPC cabe no período).

### 5.4 Extensão da `loopctl.h`

A `loopctl.h` ganha uma extensão opcional de **diagnósticos**: uma lista de nomes
e unidades mais um vetor de valores por passo. É o que alimenta o `ctl.diag`.
É uma mudança pequena no BTP 2.50, que ainda é local.

---

## 6. Cenas animadas

### 6.1 Estrutura

Cada benchmark é uma classe C++ registrada. O widget "Cena" escolhe o tipo,
liga as entradas à telemetria por nome e chama `update()` e `paint()` a cada
quadro, na taxa de renderização do TraceView.

```cpp
class SceneRenderer {
public:
    virtual ~SceneRenderer() = default;
    virtual QList<SceneInput> inputs() const = 0;     // {"x","m"}, {"theta","rad"}, {"s", array}…
    virtual QList<SceneParam> params() const = 0;     // comprimentos, cores…
    virtual void setParam(const QString& name, const QVariant& value) = 0;
    virtual void update(const SceneFrame& frame) = 0; // valores atuais + histórico
    virtual void paint(QPainter& p, const SceneView& view) = 0;  // câmera: mundo → pixels
    virtual void document(const QString& topic, const QByteArray& body) {}  // ex.: pista
};

SceneRegistry::registerType({"furuta", tr("Furuta pendulum"), [] { return new FurutaScene; }});
```

- **Desenho:** `QPainter` para o 2D, com o mesmo anti-aliasing e tema dos
  gráficos. O Furuta e o pêndulo duplo usam um **auxiliar de projeção 3D
  simples** (câmera ortográfica ou isométrica, girando com o mouse), escrito uma
  vez e reaproveitado.
- **Câmera, zoom e pan pelo mouse** vêm do widget, não de cada cena.
- **Teclado:** a cena pode mapear teclas para ações, por exemplo P/O para
  `sim.disturb`, como no pygame.
- **Ligação automática:** se a planta publica `sim.states/theta` e a cena tem a
  entrada `theta`, o editor liga sozinho.
- **Um benchmark novo é uma classe nova.** O widget, o editor e o projeto não mudam.

### 6.2 Cenas previstas

| Cena | Entradas |
|---|---|
| Robô diferencial em pista | `x`, `y`, `theta`, `s[]`, trajetória prevista (opcional) |
| Pêndulo de Furuta | `theta` (braço), `alpha` (pêndulo) |
| Carro-pêndulo (cart-pole) | `x`, `phi` |
| Pêndulo simples e pêndulo duplo | ângulos |
| Ball-and-beam | `r`, `theta` |
| Quadrotor planar | `x`, `z`, `theta` |

### 6.3 No `.tvproj`

A cena é salva só com tipo, ligações e parâmetros, **sem código**:

```json
{ "type": "scene", "scene": "furuta",
  "bind":   { "theta": { "topic": "sim.states", "field": "theta" },
              "alpha": { "topic": "sim.states", "field": "alpha" } },
  "params": { "arm_m": 0.15, "pendulum_m": 0.20 } }
```

O projeto guarda também o `.tlmodel`, a configuração da simulação (solver,
passo, período, modo) e a do controlador. Mandar o arquivo para alguém
reproduz o experimento.

**O preço:** uma cena nova exige recompilar. Se um dia outras pessoas
precisarem criar cenas sem compilar, uma cena "script" pode entrar como válvula
de escape. Hoje não é preciso.

---

## 7. Estrutura de pastas

```
lab/
  CMakeLists.txt     opção TRACEVIEW_ENABLE_LAB (desligada em Android/iOS) e os alvos abaixo
  sim/               tracelab_sim      modelo, compilador de expressões, solver, agendador (C++ sem Qt)
  plant/             tracelab_plant    planta como dispositivo BTP, manifest gerado do modelo, loopback/TCP
  control/           tracelab_control  host de controlador, adaptadores DLL/JS/externo, diagnósticos
  scenes/            tracelab_scenes   SceneRenderer, registro, widget Cena, auxiliar 3D, cenas
  module/            tracelab_module   registro do módulo no TraceView (liga/desliga, aba, identidade)
  templates/         template CMake de controlador loopctl (com OSQP)
tools/
  tracelab-cli       execução sem interface, para lote (substitui o compare_controllers.py)
```

O núcleo `tracelab_sim` não depende de Qt, o que facilita testes e uso no CLI.

---

## 8. Quem faz o quê

| Parte | Responsável |
|---|---|
| Motor de simulação (`sim/`: modelo, ExprTk, SUNDIALS, agendador), CLI e testes de validação | **você** |
| Estrutura: pastas, alvos CMake, interfaces (headers), esqueletos vazios, registro do módulo | **Claude**, sem lógica |
| As demais fases (§9), depois do esqueleto | a combinar fase a fase |

**Validação sugerida para o motor:**
- testes contra solução analítica: degrau de 1ª ordem, oscilador harmônico e
  ordem de convergência de cada método;
- regressão contra os CSVs do simulador Python: Euler a 80 Hz reproduzindo
  `runs/` dentro de uma tolerância.

---

## 9. Fases

| Fase | O quê | Depende de |
|---|---|---|
| **T0** | Esqueleto de `lab/` (§7) com interfaces e o módulo registrado | — |
| **T1** | Motor de simulação, CLI e validação (**você**) | T0 |
| **T2** | Planta como dispositivo BTP: manifest gerado, loopback, TCP, ações `sim.*`, rede simulada | T0, T1 |
| **T3** | Host de controlador: DLL `loopctl` com recarga, JS, diagnósticos, template OSQP | T2 |
| **T4** | Cliente BTP em Python e o MPC atual fechando a malha | T2 |
| **T5** | Cenas: widget, registro, auxiliar 3D, robô diferencial e Furuta | T0 (pode correr em paralelo com T2) |
| **T6** | Interface do TraceLab: editor de `.tlmodel`, painel de execução, persistência no `.tvproj` | T2, T3, T5 |
| **T7** | Ambiente "pista" (`.tfg`) e modelo do seguidor completo | T1, T5 |
| **T8** | MPC em DLL com OSQP | T3 |
| — | Estágios 2 e 3 (robô real, embarcado) e HIL | [PLANO_PERFIS_E_MALHA.md](PLANO_PERFIS_E_MALHA.md), F5–F7 |

---

## 10. O que já está pronto

| Repositório | Branch | O quê |
|---|---|---|
| BTP | `feature/stream-actions` (local) | 2.50.0: flag `STREAM`, ações no `Catalog`, `Node::send_stream()`, perfil `loop`, `loop.hpp`, `loopctl.h` |
| TraceView | `feature/bind-by-name` (local) | ligação por nome e conversão dos `.tvproj` antigos |
| TraceView | `feature/auto-chart-config` (local) | "+ Adicionar todos os campos" e unidade/faixa vindas do manifest |

Nada foi publicado no GitHub.

---

## 11. Riscos

| Risco | Mitigação |
|---|---|
| ExprTk deixa o build lento | Isolar o header num único `.cpp` |
| SUNDIALS aumenta o primeiro configure | `FetchContent` com só os módulos usados (CVODE, ARKODE) |
| Controlador que funciona no PC e é lento no ESP32 | A simulação valida a lógica, não o tempo de execução. Medir no estágio 3 |
| Usar sem perceber informação que o robô não tem | `sim.truth` e `sim.preview` separados e explícitos (L5) |
| DLL com ABI incompatível | Interface em C puro (`loopctl.h`), sem C++ na fronteira |
| Mais de uma cena aberta pesando na renderização | As cenas desenham na taxa do TraceView, que já é independente da telemetria |

---

## 12. Em aberto

1. **Primeiro conjunto de cenas:** só robô diferencial e Furuta, ou também
   cart-pole e pêndulo duplo?
2. **O esqueleto (T0) já inclui o registro do módulo na janela principal**
   (liga/desliga, aba "TraceLab", `--mode lab`), ou só a pasta `lab/` com as
   interfaces?
3. **Roteiros de experimento em JS** (degrau, perturbação, varredura de ganho):
   entram depois, se você sentir falta.
4. **Widget de array em barras** (sensores lado a lado): fica para quando a cena e
   os gráficos estiverem rodando.
5. **Tag `v2.50.0` do BTP:** fica local por enquanto (sem push). O TraceLab vai
   precisar dela quando a planta passar a usar as ações tipadas.
