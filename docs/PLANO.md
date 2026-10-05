# RTR-Bench — Real-time Raspberry Bench

Plano e requisitos. Documento de planejamento em português; produto, código,
comentários, scripts e README em inglês. Repositório próprio
(`HermesSilva/RTR-Bench`, público, Apache-2.0), pasta
`D:\Tootega\Source\RTR-SO\RTR-Bench` (dentro da pasta do RTR-OS por
conveniência; ignorada pelo git do RTR-OS).

Estado (2026-10-03): **etapas 0 a 3 feitas** — janela recortada, `Probe`,
sondas do emulador e de demonstração (sinais sintéticos com analógico),
rack com portas vivas e tecla WIRES, osciloscópio misto (digital + analógico,
trigger, cursores, medições, canais MATH com fórmulas de 1 a 4 variáveis),
fios pelo desktop (janelas de sobreposição, escondidas sem foco),
persistência em `.RTR-Bench` (bench.json, scope.json), captura composta
(`--screenshot bench=`), versão `0.1.<build>` com `build-number.txt`, CD no
GitHub Actions (Windows + Linux, release por push). Verificado no Windows;
no Linux compila e os testes passam (janela não vista: WSLg sem cliente RDP
na sessão). Observação do protocolo: o chardev do QEMU aceita um cliente por
vez. **Acréscimos de 2026-10-03 (manhã, depois dos aparelhos)**: instâncias
de aparelho por Ctrl+clique (`scope-2.json`); ligação aparelho→aparelho por
**portas virtuais** (saída do gerador/fonte publica a forma de onda, o bench
gera os eventos; só saída→entrada; `--link`); pegar o fio pelo jack ou a 20 px
da ponta; botão direito seleciona canal no osciloscópio; gerador com dropdown
de ondas (níveis, digitais com burst/sweep, analógicas, modulações AM/FM/PM/
PWM) e saídas analógicas AO0/AO1 na DEMO; pontas/saídas como mini-módulos em
duas colunas, sem textos explicativos; todas as janelas com 940 px (duas lado
a lado em Full HD); rack com uma fila de teclas e bornes em toda a largura;
fios a 2,4 px; capturas nos três temas (`--theme`, `--place`).
**Correções de 2026-10-03 (tarde)**: displays de sete segmentos com
`panel::seven_display` (valor encolhe para caber; fonte com VOLTAGE/CURRENT e
tensão do knob; estatísticas do DMM na linha de cima); teclas ON/OFF;
multímetro com dropdown de função (V DC/AC/PP, FREQ, PERIOD, DUTY, WIDTH,
COUNT, LEVEL em qualquer borne) e borne **COM** por ponta (livre = massa;
ligado = leitura diferencial; dois canais por ponta); fonte publica DC em volts
e toda saída virtual analógica emite também o nível lógico; relógio das portas
virtuais preso ao último instante da sonda (uma só linha de tempo); faixa densa
da pista digital só para transições no mesmo pixel; dropdown abre para cima
quando não cabe; ligação recusada larga o cabo; tecla do canal no osciloscópio
só seleciona; `--psu N=VOLTS`; `scripts\shots.ps1` regenera as capturas demo.
**Etapas 4, 5 (parte da bancada) e 6 feitas** (2026-10-03, manhã):
multímetro, gerador de padrões, fonte DC e analisador lógico, todos com a
tecla **ADD** (pedido do usuário: cada clique cria uma nova ponta de medida
ou saída, para um aparelho servir como vários; a janela cresce com as
linhas). A sonda DEMO ganhou portas de entrada IN0–IN3 comandadas pela
bancada (loopback) e a interface `Probe` o comando de padrão
(`drive_pattern`). Falta: sonda v2 no fork do qemu-pi4 (injeção no Pi),
gravação/replay (7), M2k (8).

## 1. Decisões tomadas

| # | Decisão | Escolha | Motivo |
|---|---------|---------|--------|
| D1 | Nome | **RTR-Bench — Real-time Raspberry Bench**, executável `rtr-bench` | mesma família do RTR-OS; "bench" é o termo da área |
| D2 | Base | app próprio em **C++17, Dear ImGui + ImPlot + imgui-knobs, GLFW + OpenGL 3.3, CMake** | nada pronto fala com a nossa sonda; as suítes completas são Qt ou Vulkan. Converter o ngscopeclient (Vulkan compute, VkFFT) foi avaliado e descartado; ele pode entrar depois só como driver scopehal sobre o mesmo protocolo |
| D3 | Plataformas | Windows e Linux (X11 e Wayland), mesma base de código | exigência do usuário |
| D4 | Sonda abstrata | instrumentos falam com uma interface `Probe`; drivers plugáveis: emulador (rtr-scope), ADALM2000 (libm2k), replay (VCD) | a bancada tem de servir ao emulador, à placa física e a alvos além do Raspberry |
| D5 | Hardware físico | **ADALM2000** via libm2k (C++, LGPL): 16 DIO 3,3 V tolerante a 5 V a 100 MS/s; scope 2 ch ±25 V 100 MS/s; gerador 2 ch ±5 V 150 MS/s; fonte ±5 V; voltímetro | um aparelho entrega todos os instrumentos com API aberta nos dois SOs. Red Pitaya fica como opção futura (14 bits, rede, FPGA) |
| D6 | Analógico | sem comparador virtual: o canal analógico existe só onde a sonda declara (M2k); no emulador tudo é lógico | honestidade da medida; nada de fingir tensão |
| D7 | Aparência | **cara de aparelho real** — referências Rigol DHO924S e Siglent SNA6034A: chassi escuro, tela com grade e leituras, botões rotativos e teclas, conectores na base | exigência do usuário: "altíssimo nível, elegante, funcional" |
| D8 | Janelas | **um aparelho por janela do SO**, livre para qualquer posição/monitor, **sem borda do SO e com o recorte do chassi** (framebuffer transparente) | o usuário quer os aparelhos espalhados pela mesa como na bancada real |
| D9 | Hub | **mini rack**: janela pequena com as portas do alvo; escolhe a sonda, abre os aparelhos, mostra os fios | faz o papel da mesa |
| D10 | Fios | ligação por clique em qualquer ponta (porta do rack ou ponta do aparelho); **terra automático e invisível** | ninguém liga GND numa bancada virtual |
| D11 | Fio entre janelas | fio desenhado **dentro das janelas** (plugue com etiqueta nas duas pontas) como base; **fio contínuo pelo desktop** via janela sobreposta clique-através onde o sistema permite (Windows, X11), com queda automática para a base (Wayland) | Wayland não dá posição de janelas |
| D12 | Conjunto v1 | osciloscópio, analisador lógico, gerador, fonte DC, multímetro | pedido do usuário; espectro, decodificadores e barramentos depois |

### 1.1 Decisões da bancada de circuitos (2026-10-04)

| # | Decisão | Escolha | Motivo |
|---|---------|---------|--------|
| D13 | Motor analógico | **ngspice** (`libngspice`), motor único, também no circuito vivo; biblioteca dinâmica carregada em execução (`ngspice.dll` / `libngspice.so.0` ao lado do executável), sem modificações. Fork com ligação estática só se for preciso mexer por dentro | o mais depurado e completo; medido em 2026-10-04: filtro RC e circuito com todas as peças a 1,00× do tempo real com passo de 20 µs |
| D14 | Esquema | **editor próprio** dentro da bancada, de alto nível, sempre vivo (sem botão "simular"), esquema com símbolos; janela redimensionável, pode cobrir mais de um monitor | pedido do usuário: "100% vivo, como uma bancada real" |
| D15 | Ligação com a bancada | qualquer ponto do esquema aceita o cabo de um aparelho (**porta de circuito**, 20000 + n); entrada lê a tensão do nó, saída comanda o nó (fonte: 50 mΩ; gerador: 50 Ω). Terra do circuito = terra da bancada (D10 mantida). Revê a D6: no circuito simulado a tensão é a do simulador | o mesmo gesto dos outros fios |
| D16 | Sem script em execução | nada de Python nem de script rodando com o programa, nem os do ngspice (`spinit`, `.control`): a netlist é gerada em memória e o motor é comandado pela API. Scripts só para build e ferramentas | exigência do usuário. Tira a libsigrokdecode (Python) |
| D17 | Biblioteca de peças | três níveis: núcleo vivo desenhado por nós sobre os modelos do ngspice; catálogo de peças reais (parâmetros SPICE); importação (`.kicad_sym`, `.lib`/`.subckt` de fabricante). Módulos: qualquer circuito salvo como bloco. Sem limites de queima (a "fumaça" era brincadeira) | o que há pronto cobre comportamento e a cauda longa de símbolos |
| D18 | Digital | lógica pelo XSPICE do próprio ngspice, na mesma netlist; processadores como peças: o Pi emulado (GPIO no esquema) e depois AVR pelo simavr; HDL só se for pedido | "por enquanto" (usuário) |
| D19 | Aparelhos | os nossos, com bibliotecas abertas nas partes pesadas (FFT com KISS FFT/PFFFT; Bode pela análise AC do ngspice; decodificadores escritos por nós ou portados da libscopehal); exportar VCD para o PulseView | trocar por aparelhos prontos perderia a aparência e a integração |
| D20 | Licença | fica para o fim; qualquer licença aberta serve (o projeto é didático), o que permite peças GPL (libsigrok, biblioteca do Qucs-S, simavr) | decisão do usuário |
| D21 | Áudio do PC | **miniaudio**; microfone, entrada de linha e caixas como bornes no rack | um cabeçalho, WASAPI e ALSA/PulseAudio |

**Feito em 2026-10-04 (primeiro corte da bancada de circuitos, LAB-1)**:
`core/circuit` (peças, fios, nós, netlist, valores "4k7"), `sim/ngspice`
(sessão: transiente sem fim em corridas encadeadas que herdam o estado,
fontes externas comandadas pela bancada, reamostragem a 50 kS/s, ritmo pelo
relógio da bancada), `instruments/circuit` (editor, peças vivas, cabos em
qualquer ponto, cor do fio pela tensão, LED aceso pela corrente), corrente na
fonte (PSU agora 0 a 30 V), `--circuit`, `rtr-sim-check`, `get-ngspice.ps1`
(ngspice 46; o pacote do 47 pede `sndfile.dll` e `samplerate.dll`).
Observações: `ngSpice_nospinit` antes de `ngSpice_Init` derruba o 46 (não é
chamada; só sai um aviso de `spinit` ausente); o ngspice guarda todos os
pontos de uma análise, por isso as corridas encadeadas (alguns segundos cada,
`.ic` + `uic` na seguinte); potenciômetro e chave são fontes B comandadas por
uma fonte externa, para girar sem recarregar a netlist.

**Acréscimos de 2026-10-04 (noite)**: projetos do LAB (teclas NEW, OPEN,
SAVE; um arquivo JSON por projeto, com o circuito e os cabos, na pasta
`.RT-Lab` ao lado do executável; o nome aparece no chassi); alfinete
"stay on top" em todas as janelas (rack, aparelhos e LAB), guardado com a
posição em `bench.json`; `--psu` aceita até 30 V.

**LAB profissional, primeira leva (2026-10-05)** — orientação do usuário:
tomar como base a usabilidade dos melhores laboratórios (Multisim, Proteus,
LTspice, TINA), inclusive comerciais, e ir além. Feito: tecla **ADD** com
diálogo de catálogo (categorias, busca, pré-visualização do símbolo; a peça
escolhida fica na mão); **catálogo** em `core/circuit.cpp` (peça genérica de
cada tipo + peças reais com modelo SPICE: 1N4148, 1N4007, 1N5819, zeners,
LEDs por cor, 2N3904, 2N2222, BC547B, 2N3906, BC557B, LM358, TL072, NE5532,
MOSFETs de nível 1); **painel de propriedades** da peça selecionada (peça do
catálogo, valor, frequência, posição, girar, remover, e o que ela faz agora:
tensão por pino, corrente, potência, Vbe/Vce, Vgs/Vds); peças novas: fonte
DC, fonte senoidal, fonte de corrente, NMOS, PMOS, **voltímetro e
amperímetro de painel** com leitura no esquema; desfazer/refazer
(Ctrl+Z/Ctrl+Y, 200 níveis), duplicar (Ctrl+D), menu de contexto (botão
direito), RUN/PAUSED (espaço), FIT (F), exportar netlist SPICE (`.cir` na
pasta dos projetos). Os parâmetros dos modelos reais foram escritos de
memória a partir dos modelos publicados: conferir com as folhas dos
fabricantes antes de confiar em números finos.

**Corrente no multímetro (2026-10-05)**: funções **A DC** e **A AC** no
DMM-1. Com a ponta e o COM em dois pontos do esquema o aparelho entra em
série (fonte de 0 V + 10 mΩ entre os dois nós; COM livre = terra da
bancada) e a ponta recebe a corrente em amperes no lugar da tensão.
Conferido no projeto `led-current`: 4,054 mA no DMM contra 4,04 mA no
amperímetro de painel. `--dmm N=FUNÇÃO` escolhe a função na partida. A linha
de mín/máx/média ainda mostra amperes sem prefixo.

**Áudio do PC no rack (2026-10-05, D21 feita)**: `src/audio/audio_ports`
(miniaudio 0.11.21). Cada dispositivo de captura e de reprodução é um
**grupo** na faixa AUDIO do rack, com o nome do dispositivo e um borne por
canal (L/R); o padrão do sistema vem primeiro; a faixa quebra em linhas e a
janela do rack cresce. Para a aplicação é um aparelho sem janela
(`Instrument::Audio`): entrada ligada a um ponto do esquema vira fonte
(600 Ω, 1 V = fundo de escala, 30 ms atrás da simulação), ponto do esquema
ligado a uma saída é tocado (reamostrado para 48 kHz). O dispositivo só
abre enquanto tem cabo. Conferido: microfone → RC → osciloscópio e caixas a
1,00×. Falta: entrada de áudio direto num aparelho (sem passar pelo
esquema), ganho por borne, medir o estalo na emenda das corridas do ngspice,
Linux.

**Som de qualquer player no rack (2026-10-05)**: pedido do usuário — uma
saída virtual visível no Windows que chega ao rack. Um programa não cria
dispositivo de áudio sem driver (instalação com administrador, fora do
executável portátil). Feito o caminho sem driver: grupo **PLAYING** por
dispositivo de reprodução (WASAPI loopback, só Windows), que entrega ao
esquema o que o PC está tocando ali; a saída OUT do mesmo dispositivo fica
muda enquanto o PLAYING dele estiver em uso (realimentação). Conferido com
som tocando no PC: sinal no osciloscópio antes e depois do RC, 1,00×.
Caminho com driver, sem código nosso: um cabo virtual instalado pelo
usuário (VB-CABLE ou equivalente) aparece como saída no Windows e como
grupo IN no rack. **Em aberto (decisão do usuário)**: empacotar ou não um
driver de cabo virtual de código aberto junto do projeto.

**RTR-Cable (2026-10-05)**: decisão do usuário — ter o nosso cabo virtual
de código aberto, instalado à parte. Repositório `HermesSilva/RTR-Cable`
(fork MIT do AudioMirror; clone em `RTR-SO\RTR-Cable`). Cria "RTR-Cable
Input" (reprodução) e "RTR-Cable Output" (gravação, que aparece como grupo
IN no rack). O CI compila, verifica o INF e assina o pacote com certificado
de teste. **Limite**: só carrega com o Windows em modo de teste (Secure Boot
desligado); assinatura de produção pede certificado EV e o Hardware Dev
Center da Microsoft. Instalação e áudio pelo cabo ainda não testados.

**Rack de áudio, acréscimos de 2026-10-05**: lâmpada de sinal ao lado de
cada borne (verde com sinal, vermelha perto do fundo de escala; as fontes
ficam abertas desde a partida para isso); **borne a borne** no próprio rack
(fonte → saída, cabo desenhado no rack, salvo em `bench.json`, `--patch
A=B`), recusado quando a saída é o dispositivo que a fonte PLAYING escuta;
VB-CABLE como cabo virtual por enquanto (`scripts\get-vbcable.ps1`,
`extras\vb-cable`, binários fora do git).

**Capturas (2026-10-05, decisão do usuário)**: só no tema **dark**, e cada
imagem é aberta e conferida antes do commit (substitui "três temas" da RV6
para as capturas; os temas continuam no produto). As cenas compostas usam
`--tile on`: a bancada arruma as janelas em duas colunas, sem sobreposição
e dentro de um monitor (uma janela que cai fora da tela passa para outro
monitor e é desenhada na escala dele). As duas cenas do emulador
(`rack-dark.png`, `scope-dark.png`) ainda são as antigas.

**Próximas levas, pela ordem**: (2) editor — seleção múltipla com retângulo,
copiar/colar, mover fio, fios que acompanham em ângulo reto, rótulos de nó
(net labels) e símbolos de alimentação VCC/VEE, texto livre; (3) análises
fora do tempo real numa janela própria — Bode (AC), varredura DC, transiente
de precisão, Fourier/THD, ruído, varredura de parâmetro; (4) mais peças —
transformador, relé, lâmpada, buzzer/alto-falante, motor DC, 555, reguladores
78xx/LM317, JFET, optoacoplador, display de 7 segmentos, portas lógicas e
flip-flops (XSPICE); (5) pontas de medida no esquema com V/I/f ao vivo e
animação da corrente nos fios; (6) módulos (subcircuitos próprios),
importação de `.kicad_sym` e `.lib`/`.subckt`; (7) verificação elétrica
(nó solto, sem terra, saída em curto), lista de materiais, exportar imagem;
(8) falhas de componente (aberto, curto, fuga) para ensino de diagnóstico.

**Falta na bancada de circuitos**, pela ordem sugerida: corrente no multímetro
(ponta numa perna de peça); fonte com saída negativa/simétrica; áudio do PC
(D21); desfazer/refazer, copiar e colar, seleção múltipla; subida de nível dos
aparelhos (D19: FFT/espectro, Bode, XY, fase, estatística, exportação);
catálogo de peças reais e módulos (D17); lógica XSPICE e GPIO do Pi no esquema
(D18); medir o soluço de áudio numa edição; Linux (`libngspice0`).

## 2. Para que serve

Bancada de instrumentos para desenvolver e validar o RTR-OS e os programas de
automação que rodam nele:

- no **emulador** (qemu-pi4 com a sonda `rtr-scope`): ver o que o SO faz nos
  pinos (PWM, saídas de automação), injetar entradas (sensores, botões,
  encoders) e medir tempos — lembrando que o tempo do emulador é lógico, não
  físico;
- na **placa física** (Pi 4 ou qualquer alvo de 3,3 V/5 V) com o ADALM2000:
  as mesmas telas, agora com tempo e tensão reais;
- **comparar** capturas do emulador e da placa lado a lado, com o mesmo formato.

## 3. Requisitos

### 3.1 Gerais (RG)

- RG1. Um processo `rtr-bench`, várias janelas; uma sonda ativa por vez,
  compartilhada por todos os aparelhos.
- RG2. 60 quadros/s com todos os aparelhos abertos e a sonda a 100 MS/s de
  entrada digital; a interface nunca trava por causa da sonda (aquisição em
  thread própria, fila sem bloqueio).
- RG3. Tudo em inglês na interface. Unidades SI com prefixo (ns, µs, ms, s;
  mV, V; Hz, kHz, MHz).
- RG4. HiDPI: escala por monitor, fontes nítidas; janelas podem ir a monitores
  de escalas diferentes.
- RG5. Teclado completo em cada aparelho (Run/Stop = espaço, Single, Auto,
  setas para posição, +/− para escala), como no painel real.
- RG6. Estado persistente em **JSON**: posição e tamanho de cada janela,
  aparelhos abertos, fios, ajustes de cada aparelho, sonda escolhida —
  recarrega ao abrir. Um arquivo por assunto (`bench.json`, `scope.json`,
  …) para que um ajuste não reescreva tudo.
- RG7. **Executável portátil**: sem instalador, sem registro, sem diretório do
  usuário. Tudo que o programa grava vai para a pasta **`.RTR-Bench`** criada
  ao lado do executável: configurações, gravações, exportações, logs. Copiar a
  pasta do programa leva tudo junto; apagar `.RTR-Bench` volta ao padrão.
  Recursos (fontes) embutidos no binário. No Linux, o mesmo binário (e um
  AppImage opcional, que usa a pasta ao lado do AppImage).

### 3.2 Sonda e drivers (RS)

- RS1. Interface `Probe` com capacidades declaradas: lista de portas (nome,
  número, direção possível, digital/analógico), pode observar, pode injetar,
  resolução de tempo (ns), taxa máxima de amostragem, faixa de tensão.
- RS2. Dados em ambos os sentidos com **tempo em nanossegundos** no relógio da
  sonda: eventos digitais (porta, nível, tempo) e blocos analógicos (porta,
  taxa, amostras em volts, tempo inicial).
- RS3. `EmulatorProbe`: TCP ao `rtr-scope` (127.0.0.1:5555). Hoje a sonda só
  transmite transições de saída (`<ns> <pin> <level>`) e envia um retrato ao
  receber qualquer byte. **A sonda ganha**: (a) comando para forçar o nível de
  uma porta de entrada (gerador, fonte); (b) comando de retrato com a lista de
  portas e direções (GPFSEL); (c) versão do protocolo; (d) opcional: relógio
  virtual atual para o rack mostrar o tempo do emulador. Mudança em
  `tools/qemu-pi4/rtr_scope.c` no RTR-OS, documentada nos dois repositórios.
- RS4. `M2kProbe`: libm2k — `Digital` (analisador lógico + gerador de padrões,
  16 DIO), `AnalogIn` (scope/voltímetro), `AnalogOut` (gerador), `PowerSupply`,
  `DMM`. Calibração do aparelho ao conectar. Dependência opcional em tempo de
  compilação (`RTR_BENCH_WITH_M2K`), carregada em tempo de execução se a
  biblioteca existir.
- RS5. `ReplayProbe`: reproduz uma captura gravada (VCD ou formato próprio) com
  o tempo original; permite "pausar o tempo" e andar quadro a quadro.
- RS6. Troca de sonda sem fechar os aparelhos: os fios ligados a portas que não
  existem na nova sonda ficam "soltos" (marcados), não apagados.
- RS7. Perda de conexão: o rack mostra o estado (connected / reconnecting /
  disconnected), reconecta sozinho a cada segundo, e os aparelhos congelam a
  última tela com marca "no signal".
- RS8. Mapa de portas por sonda: Pi 4 = GPIO 0–27 do conector de 40 pinos (com
  o número físico do pino ao lado, BCM e board); M2k = DIO 0–15, 1+/1−, 2+/2−,
  W1, W2, V+, V−.

### 3.3 Mini rack (RR)

- RR1. Janela pequena com cara de painel de rack (1U/2U), na horizontal, com as
  portas do alvo como jacks numerados e etiquetados; cor do jack = estado
  (entrada, saída, flutuando, sem informação).
- RR2. Cada jack mostra o nível atual (LED aceso/apagado) e um mini-indicador
  de atividade (pisca em transição) — dá para ver o PWM "vivo" sem abrir scope.
- RR3. Seletor de sonda (Emulator / ADALM2000 / Replay) com estado da ligação
  e, no emulador, o endereço; no M2k, número de série e temperatura.
- RR4. Botões para abrir cada aparelho (Scope, Logic, Gen, PSU, DMM); abrir um
  aparelho já aberto traz a janela para frente.
- RR5. Clicar num jack: se tem fio, destaca o fio e o aparelho de destino
  (pisca a ponta correspondente); se não tem, inicia um fio que termina ao
  clicar numa ponta de aparelho ou cancela com Esc.
- RR6. Um jack pode alimentar várias entradas de aparelhos (scope e multímetro
  no mesmo GPIO); uma porta de entrada do alvo só aceita **um** driver
  (gerador ou fonte), com aviso ao tentar o segundo.
- RR7. Relógio do alvo e contador de eventos por segundo na barra do rack.

### 3.4 Fios (RW)

- RW1. Cor do fio = cor do canal do aparelho (CH1 amarelo, CH2 ciano, CH3
  magenta, CH4 azul, lógico verde, gerador laranja, fonte vermelho, multímetro
  branco).
- RW2. Dentro das janelas: plugue desenhado no jack/ponta, pedaço de cabo até a
  borda, etiqueta com o destino ("SCOPE CH1" / "GPIO 18").
- RW3. Fio contínuo (Windows, X11): janela sobreposta por monitor, transparente
  e clique-através, desenha cabo com curva e sombra entre as duas pontas;
  acompanha as janelas ao mover; some ao minimizar. Em Wayland, não existe e
  nada quebra.
- RW4. Arrastar com o mouse entre janelas: o cabo segue o cursor; soltar sobre
  uma ponta válida liga; sobre nada, cancela.
- RW5. Terra: nunca aparece. Toda ponta de aparelho é referenciada ao GND do
  alvo automaticamente.

### 3.5 Aparência e janelas (RV)

- RV1. Cada aparelho é uma janela GLFW sem decoração, framebuffer transparente;
  o chassi é desenhado com alfa, o que fica fora é transparente (recorte).
- RV2. Arrastar pelo chassi; redimensionar pelos cantos mantendo proporção;
  botão de fechar e de minimizar desenhados no chassi; duplo clique no
  "rodapé" encaixa o tamanho padrão.
- RV3. Linguagem visual única: chassi grafite com textura leve, botões
  rotativos com marcador luminoso e sombra, teclas com iluminação quando ativas
  (RUN verde / STOP vermelho, como no Rigol), tela com fósforo escuro, grade
  8×10 e leituras em fonte de sete segmentos onde fizer sentido.
- RV4. Botões rotativos: arrastar vertical ou roda do mouse; clique = função
  secundária (zero, fine); duplo clique = reset. Teclas com retorno visual de
  pressão.
- RV5. Tela de cada aparelho com ImPlot ou draw list próprio, antialiasing,
  persistência configurável (fósforo), intensidade.
- RV6. **Três temas: Light, Dark e Amber**, como na interface web do RTR-OS,
  aplicados a chassi, tela, teclas e leituras (Dark = grafite das referências;
  Light = chassi claro tipo bancada HP/Keysight antiga; Amber = fósforo âmbar
  e teclas quentes). Escolha no rack, guardada em `bench.json`; troca ao
  vivo em todas as janelas. Cores dos canais ajustáveis à parte (daltonismo).
- RV7. Fontes embutidas no executável: uma sem serifa para o painel, uma mono
  para leituras, DSEG7 para mostradores.
- RV8. Sem janelas, popups ou widgets com estilo padrão do ImGui visíveis ao
  usuário.

### 3.6 Osciloscópio (RO)

- RO1. 4 canais; digitais em qualquer sonda, analógicos onde a sonda tem
  (M2k: 2). Cada canal tem ponta própria (fio).
- RO2. Horizontal: tempo/div de 10 ns a 10 s em 1-2-5, posição, zoom (janela de
  zoom como no Rigol), roll automático acima de 100 ms/div.
- RO3. Vertical (analógico): volts/div 1-2-5 de 10 mV a 10 V, posição,
  acoplamento DC/AC, inversão, largura de banda limitada. Digital: altura e
  posição do traço.
- RO4. Trigger: fonte (qualquer canal), borda subida/descida/ambas, nível
  (analógico), modo auto/normal/single, holdoff, pré-trigger ajustável
  (posição). Trigger por largura de pulso (maior/menor que) na v1; padrão e
  runt depois.
- RO5. Run/Stop, Single, Auto (ajusta escala e trigger para o sinal
  presente), Clear.
- RO6. Cursores: tempo (A, B, Δ, 1/Δ) e tensão; arrastáveis na tela.
- RO7. Medições automáticas por canal (até 8 ao mesmo tempo, com estatística
  mín/máx/média/desvio): frequência, período, largura +/−, duty, tempo de
  subida/descida (analógico), Vpp, Vmáx, Vmín, Vmédio, Vrms, contagem de
  pulsos, fase entre canais.
- RO8. Memória: 1 M eventos digitais por canal / 1 M amostras analógicas;
  rolagem pela memória quando parado.
- RO9. Gravar/exportar: VCD (digital), CSV (analógico), PNG da tela (com as
  leituras), e "snapshot" como referência que fica desenhada por trás.
- RO10. Sob o emulador: a escala de tempo é a do relógio virtual; a tela avisa
  "virtual time" na barra de estado.
- RO11. **Canais MATH** (pedido em 2026-10-03): traços resultantes de uma
  fórmula sobre **2 ou 3 canais de entrada**, escolhida numa lista em combo
  elegante (estilo "Math" dos Rigol). Digitais: NOT, AND, OR, XOR, NAND, NOR,
  XNOR, A·B·C, A+B+C, A⊕B⊕C, maioria(A,B,C), latch SR (A set, B reset),
  A gated by C (A·C), diferença de fase A→B como pulso; analógicas (M2k): A+B,
  A−B, A×B, A/B, média(A,B,C), FFT(A). Até 2 canais MATH, cada um com cor
  própria, medições e cursores como os demais; a fórmula e as entradas ficam
  na barra de leituras ("M1 = CH1 AND CH2").

### 3.7 Analisador lógico (RL)

- RL1. Até 16 canais (todas as portas digitais do alvo), nomeáveis, agrupáveis
  em barramentos (ex.: 4 bits = valor hex).
- RL2. Mesma base de tempo e trigger do scope (trigger por padrão em N canais
  na v1).
- RL3. Visão de barramento com valores; visão de pulsos; busca por evento
  (próxima borda, próximo valor).
- RL4. Decodificadores: v2 (UART, SPI, I²C, PWM/servo, encoder quadratura).

### 3.8 Gerador (RGn)

- RGn1. 2 canais. Digital em qualquer sonda que injeta; analógico só no M2k.
- RGn2. Digital: nível fixo, clock (freq, duty), PWM (freq, duty, fase),
  pulso único/burst (N pulsos, largura), sequência de bits (padrão, taxa),
  encoder quadratura (A/B, freq, sentido), varredura de frequência.
- RGn3. Analógico (M2k): seno, quadrada, triângulo, rampa, ruído, DC,
  arbitrário por CSV; amplitude, offset, frequência, fase, simetria.
- RGn4. Saída ligada/desligada por tecla com LED; mostrador com a forma
  escolhida e os parâmetros; botão rotativo com dígito selecionável (como no
  gerador real: cursor no dígito, roda muda aquele dígito).
- RGn5. Sincronismo entre os dois canais (fase relativa) e com o trigger do
  scope (saída "sync").

### 3.9 Fonte DC (RP)

- RP1. 2 saídas. No emulador: força nível alto/baixo numa porta de entrada,
  com "tensão" apresentada só como 0/3,3 V. No M2k: V+ 0 a +5 V, V− 0 a −5 V,
  leitura de tensão e corrente reais.
- RP2. Painel com dois mostradores (V e A) por saída, ajuste por botão
  rotativo com dígito selecionável, tecla Output com LED, limite de corrente
  (M2k).
- RP3. Proteção: ao trocar de sonda ou perder conexão, saídas desligam.

### 3.10 Multímetro (RM)

- RM1. Uma ponta (fio) e uma função por vez: V DC, V AC (rms), Hz, duty, largura
  de pulso, contador de pulsos (com reset), tempo em alto/baixo acumulado,
  nível lógico (H/L/toggling).
- RM2. Mostrador grande de sete segmentos (5 dígitos) + barra analógica
  (galvanômetro desenhado) + taxa de atualização ajustável (2/5/10 por s).
- RM3. Min/Max/Avg com hold; registro (log) em CSV com intervalo.
- RM4. No emulador, V só mostra 0 / 3,30 V (lógico); no M2k, mede de verdade.

### 3.11 Gravação, replay e comparação (RA)

- RA1. Gravar tudo o que a sonda entrega numa sessão (formato próprio
  compacto + exportação VCD/CSV).
- RA2. `ReplayProbe` toca a gravação como se fosse a sonda; os aparelhos nem
  sabem.
- RA3. Comparação: scope com canal "reference" carregado de uma gravação
  (emulador vs placa), com alinhamento por primeira borda.

### 3.12 Desempenho (RD)

- RD1. Aquisição em thread própria, fila lock-free para a thread de interface;
  decimação para a tela (mín/máx por pixel) feita a cada quadro.
- RD2. Scope digital com 1 M eventos renderiza a 60 fps; analógico idem com
  decimação.
- RD3. Fio contínuo e janelas transparentes não podem derrubar o quadro: a
  janela sobreposta redesenha só quando algo muda.

### 3.13 Qualidade e código (RQ)

- RQ1. C++17, `-Wall -Wextra -Werror -Wconversion -Wshadow`, clang-tidy com o
  mesmo espírito do RTR-OS (dependências externas isentas).
- RQ2. Estrutura: `core/` (Probe, dados, gravação), `probes/` (emulator, m2k,
  replay), `ui/` (chassi, widgets, janelas, fios), `instruments/` (um por
  aparelho), `app/` (main, rack, configuração), `tests/`.
- RQ3. Testes sem janela: parser do protocolo da sonda, medições (frequência,
  duty etc. sobre sinais sintéticos), trigger, decimação, gravação/replay.
- RQ4. Dependências por CMake FetchContent com versão fixa: GLFW, Dear ImGui,
  ImPlot, imgui-knobs, nlohmann/json, Catch2 (testes). libm2k pelo
  pacote do sistema / instalador da ADI.
- RQ5. Licenças: **código aberto, Apache-2.0** (decidido 2026-10-03, junto com
  o RTR-OS); terceiros MIT/zlib/LGPL/OFL listados em `THIRD_PARTY.md`; libm2k
  (LGPL) usada como biblioteca dinâmica. A sonda `rtr_scope.c` vive no fork
  público `HermesSilva/qemu-pi4` sob GPL-2.0-or-later.

### 3.14 Build e distribuição (RB)

- RB1. Windows: clang ou MSVC, CMake; Linux: gcc/clang, CMake; mesmo
  `CMakeLists.txt`.
- RB2. Scripts: `build.ps1` / `build.sh`, `run.ps1` / `run.sh`, `test.*`.
- RB3. Artefatos: `rtr-bench.exe` + `resources/`; Linux: binário + AppImage.
- RB4. CI local (script) que compila nas duas plataformas (Windows nativo e
  WSL Ubuntu) antes de qualquer commit.
- RB5. **A cada release, screenshots atualizadas** de toda interface que
  mudou (rack, cada aparelho, temas quando relevante) em `docs/screenshots/`,
  tiradas por `scripts\screenshot.ps1` (mesmo tamanho e conjunto de temas), e
  colocadas no README. Release com tela mudada e screenshot velha não está
  pronta.

### 3.15 Outros aparelhos (depois da v1)

O que o ADALM2000 oferece: **captura** (2 ADC ±25 V 100 MS/s; 16 entradas
digitais 100 MS/s) e **envio** (2 DAC ±5 V 150 MS/s; 16 saídas digitais 3,3 V;
fonte V+ 0…+5 V / V− 0…−5 V, dezenas de mA, com leitura de tensão e corrente).
Não absorve corrente de forma controlada: carga eletrônica só com hardware
externo comandado por ele. Na tabela, "emulador" = o que faz sentido num GPIO
puramente lógico.

| Aparelho | Emulador | ADALM2000 / Pi físico | Observação |
|----------|----------|------------------------|------------|
| **Fonte de tensão variável** (rampa fina, varredura) | só nível 0/3,3 V | DAC W1/W2 ou V± numa entrada GPIO: varre a tensão e descobre o **limiar e a histerese** do pino, com o scope vendo quando o SO reage | separa-se da fonte DC por ter varredura, passos e tempo de permanência |
| **Carga variável / eletrônica** | — (sem corrente) | precisa de módulo externo (MOSFET + shunt): o DAC comanda, o ADC lê V e I; o painel mostra V, I, P, R com modos CC/CV/CR/CP | aparelho da bancada que exige um "acessório" físico; documentar o circuito |
| **Contador / intervalômetro** | sim | sim | frequência, período, contagem, **intervalo entre bordas de dois pinos** (entrada → saída = latência do SO), totalizador; essencial para o RTR-OS |
| **Analisador de jitter** | sim (lógico) | sim | histograma e tendência do período/largura de um pino ao longo do tempo; mín/máx/σ; alarme ao sair da faixa; é o que mede "engrenagens em sincronia" |
| **Simulador de sensores/atuadores** | sim | sim (digital) | botão com repique, chave, fim de curso, encoder quadratura com velocidade e sentido, sensor de rotação (pulsos/rev), tacômetro; roteiros no tempo (script: "em t=2 s fecha a chave") |
| **Barramento mestre** (UART, SPI, I²C) | sim, bit-bang na sonda injetando pinos | sim (libm2k tem UART/SPI/I²C por bit-bang nas DIO) | fala com o SO como um periférico; útil quando I²C/SPI entrarem no RTR-OS |
| **Analisador de protocolo** | sim | sim | decodificadores UART/SPI/I²C/PWM-servo/encoder sobre a captura do lógico (RL4) |
| **Registrador de dados** (data logger) | sim | sim | grava medições do multímetro/contador por horas com intervalo fixo; gráfico de tendência; CSV |
| **Analisador de espectro** | — | sim (ADC) | FFT dos canais analógicos, janela, média, pico; vale para ruído e ripple |
| **Analisador de resposta (Bode)** | — | sim (DAC + ADC) | varredura de frequência pelo gerador e medição de ganho/fase; filtros e malhas |
| **Traçador de curvas** | — | sim + resistor externo | V×I de diodos/transistores; fora do foco de automação, mas o M2k faz |
| **Gerador de padrões 16 bits** | sim | sim | vetores de teste por arquivo, taxa e laço; simula um barramento paralelo |

Prioridade sugerida depois da v1: contador/intervalômetro e analisador de
jitter (medem o que o RTR-OS promete), simulador de sensores (fecha a malha
no emulador), fonte variável (quando o M2k chegar), barramento mestre e
protocolo (quando I²C/SPI entrarem no SO), espectro/Bode, carga e traçador.

## 4. Arquitetura

```
┌──────────── rtr-bench (um processo) ─────────────────────────────────┐
│  app/   rack (hub), configuração, ciclo de janelas                   │
│  instruments/  scope · logic · generator · psu · dmm                 │
│  ui/    chassi, botões, teclas, tela, fios, janela transparente      │
│  core/  Probe (interface), buffers, medições, gravação, replay       │
│  probes/  EmulatorProbe (TCP 5555) · M2kProbe (libm2k) · ReplayProbe │
└──────────────────────────────────────────────────────────────────────┘
          │ rtr-scope (qemu-pi4)          │ USB (ADALM2000)
          ▼                               ▼
   emulador RTR-OS                  Pi 4 físico ou outro alvo
```

Threads: uma de aquisição por sonda; uma de interface (todas as janelas GLFW
no mesmo thread, um contexto ImGui por janela, `glfwPollEvents` único).

## 5. Etapas

| # | Etapa | Entrega verificável |
|---|-------|---------------------|
| 0 | Repositório, CMake, dependências, janela vazia transparente com chassi em Windows e Linux | abre, arrasta, fecha; recorte funciona |
| 1 | `Probe` + `EmulatorProbe` (só observar) + rack com jacks vivos | LEDs do rack seguem o PWM do emulador |
| 2 | Scope digital: tempo/div, trigger, cursores, medições, Run/Stop/Single | PWM medido igual ao `live.html` |
| 3 | Fios: dentro das janelas; sobreposição em Windows/X11 | ligar scope ao GPIO 18 pelo clique |
| 4 | Multímetro (funções digitais) | freq/duty do PWM no mostrador |
| 5 | Sonda com injeção (`rtr_scope.c`) + gerador digital + fonte lógica | o RTR-OS lê uma entrada forçada pela bancada |
| 6 | Analisador lógico com barramento | 4 bits de saída como valor hex |
| 7 | Gravação, replay, exportação VCD/CSV/PNG | replay de uma sessão sem emulador |
| 8 | `M2kProbe` (quando o aparelho chegar): digital, analógico, gerador, fonte, DMM | PWM do Pi 4 físico na tela, com tensão real |
| 9 | Polimento: HiDPI, teclado, persistência, AppImage | pacote utilizável |

## 6. Pontos em aberto

- A1. ~~Formato de persistência~~ — decidido: JSON em `.RTR-Bench` ao lado do
  executável (RG6, RG7).
- A2. Injeção no emulador: o modelo `bcm2835_gpio` do QEMU precisa aceitar
  nível externo em pinos configurados como entrada; verificar no fork
  `qemu-pi4` como os níveis de entrada são lidos (GPLEV) e se há `qemu_irq` de
  entrada por pino.
- A3. Fio contínuo no Windows: `WS_EX_LAYERED | WS_EX_TRANSPARENT` numa janela
  GLFW exige ajuste via `glfwGetWin32Window`; confirmar que não rouba foco.
- A4. Fonte DSEG7 (licença SIL OFL) e fonte do painel (Inter, OFL): confirmar
  inclusão no repositório fechado (OFL permite).
- A5. Decodificadores (UART/SPI/I²C): escrever ou embutir libsigrokdecode
  (Python) — tendência: escrever os três básicos.

## 7. Riscos

- Janelas transparentes variam entre drivers/compositores no Linux; manter um
  modo "chassi opaco com cantos retos" como queda.
- libm2k no Windows exige o driver USB da ADI; documentar a instalação.
- O ADALM2000 chega em algumas semanas: a etapa 8 fica depois das outras; o
  `Probe` nasce já com os tipos analógicos para não refazer nada.
- Desenhar aparelhos bonitos consome tempo: fixar uma biblioteca de widgets
  (`ui/`) na etapa 0–2 e reutilizar em todos.
