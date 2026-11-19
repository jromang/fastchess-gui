# Guia do usuário do fastchess-gui

Este guia descreve cada página, configuração e arquivo do fastchess-gui. O [README](../README.md) diz o que é o
programa, onde baixá-lo e como compilá-lo. No programa, F1 abre este guia ao lado da página, na seção do que está sob
o mouse, no idioma da interface (veja [Ajuda no programa](#ajuda-no-programa)).

## Início rápido
<!-- en: quick-start 92353f2c -->

### Seu primeiro torneio
<!-- en: your-first-tournament 75e9a891 -->

1. **Engines.** Na barra à esquerda, *Engines*, depois *Adicionar…* e escolha o programa de um engine (um executável
   UCI ou WinBoard): ele é iniciado uma vez para ler o nome e as opções. *Importar…* > *Uma lista de engines…* traz
   os engines de outro programa (cutechess, Banksia, Arena, ChessGUI), *Importar…* > *Todos os engines de uma
   pasta…* todos os programas de uma pasta. *Verificar* diz se um engine funciona.
2. **Novo torneio.** *Torneios* > *Novo torneio*. Escolha um *Formato* na lista (*Todos contra todos*,
   *Gauntlet*…) e um *Controle de tempo* (*10+0.1* para uma primeira tentativa), e deixe as outras linhas como estão.
3. **Participantes.** Dê um duplo clique nos engines da *Biblioteca*, ao lado dos participantes, ou selecione vários
   e pressione Enter.
4. **Iniciar.** O torneio abre no seu espaço de trabalho: *Visão geral* para a classificação e o tempo restante,
   *Ao vivo* para as partidas em andamento.

O resumo à direita da configuração diz quantas partidas serão jogadas e quanto tempo vão levar, e o que daria errado
(um programa não encontrado, threads demais, um livro faltando), cada coisa com um botão que a corrige.

### Testar uma nova versão de um engine
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Adicione as duas versões à biblioteca.
2. *Novo torneio*, *Formato*: *SPRT ganho [0, 3]* (a nova versão ganha força?) ou *SPRT não regressão [-5, 0]*
   (ela não é mais fraca?).
3. Escolha a nova versão como **A** e a antiga como **B**, um controle de tempo curto (*10+0.1*) e, em *Aberturas*,
   um livro de posições equilibradas (EPD), depois clique em **Iniciar**.

O match para sozinho assim que o SPRT decide: H1 aceita (A é mais forte, segundo os limites) ou H0 aceita (não é). A
*Visão geral* mostra o Elo, o LLR e seus limites à medida que as partidas chegam. A configuração é guardada: da
próxima vez, escolha a versão seguinte como A e clique em **Iniciar**, ou em *Relançar com…* no match terminado.

### Próximos passos
<!-- en: where-to-go-next 2cd9b9f7 -->

- [Novo torneio](#novo-torneio): todas as configurações, e [dicas](#dicas-para-as-configurações) para escolhê-las;
- [Um torneio em andamento](#um-torneio-em-andamento): o que as abas mostram, pausar, alterar um torneio enquanto ele
  é jogado;
- [Ratings](#ratings): uma lista de rating sobre todas as partidas jogadas;
- [Transmissão](#transmissão): as partidas ao vivo numa página web, no TLCV ou no lichess;
- [Solução de problemas](#solução-de-problemas) quando algo dá errado, e o [Glossário](#glossário) para as palavras
  dos testadores.

## A janela
<!-- en: the-window 3a5230d4 -->

### As páginas
<!-- en: the-pages b13b4de2 -->

Uma barra de navegação à esquerda leva a oito páginas:

| Página | Conteúdo |
|---|---|
| Torneios | A página inicial: o que está rodando e o que requer atenção, a fila, os últimos torneios concluídos, todos os torneios do perfil, *Novo torneio* |
| Engines | A biblioteca de engines: opções, verificações, grupos, Elo de referência, importação das listas de outros programas |
| Livros | A biblioteca de livros de aberturas, e as aberturas de cada livro com o que há de errado nelas |
| Tabuleiro | Uma partida ou uma posição vinda de qualquer lugar, analisada por um engine da biblioteca |
| Suítes | Suítes de testes (STS, WAC, ERET…): engines contra as posições de um arquivo EPD |
| Ratings | Uma lista de rating sobre todas as partidas de todos os torneios |
| Log | A saída do próprio fastchess e as mensagens do programa, e *Relatar um problema…* |
| Preferências | Idioma, tema, tamanho da interface, notificações, a aparência dos tabuleiros, as pastas do perfil e dos torneios, as máquinas remotas, o token do lichess, a versão, a forma como a janela é desenhada |

### Ajuda no programa
<!-- en: help-in-the-program 351be20d -->

- **Dicas.** Deixe o mouse sobre um controle para ver o que ele faz. A dica de um controle que este guia explica com
  mais detalhes termina com *F1: mais no guia*.
- **F1** abre este guia num painel à direita da página, na seção do que está sob o mouse (uma linha da configuração,
  um cartão da página inicial, uma aba, uma coluna da classificação); sem nada sob o mouse, na seção do último painel
  clicado, senão na da página. O **?** redondo ao lado do título de uma página abre a seção da página. F1 de novo, ou
  Esc no painel, o fecha.
- **O painel do guia** mostra uma seção com suas subseções, e a página ao lado continua funcionando: dá para ler a
  explicação enquanto se altera a configuração. O cabeçalho tem *Voltar* e *Avançar* (também Alt+Esquerda e
  Alt+Direita, ou os botões laterais do mouse), *Sumário* (todas as seções), o campo de busca (Ctrl+F no painel) e
  *Abrir no navegador* (este guia no GitHub, para a versão em execução). A busca encontra todas as palavras digitadas,
  em qualquer ordem, sem contar maiúsculas e acentos; os títulos vêm primeiro, e as ocorrências ficam destacadas. Os
  links para outras seções abrem no painel, os links para páginas web no navegador, e um bloco de código tem
  *Copiar*. Arraste a borda esquerda do painel para alargá-lo; a largura é guardada.
- **Idiomas.** O guia vem embutido no programa, em todos os idiomas da interface, e acompanha a versão em execução;
  nada é buscado na rede. Uma seção cuja tradução falta ou é mais antiga que o texto em inglês é mostrada em inglês,
  com uma linha avisando. Os arquivos compactados de cada versão também o trazem (`guide.md`, e `guide.<language>.md`
  para cada tradução).
- **Ctrl+K**, a paleta de comandos, encontra as seções do guia pelos títulos (*Guia: …*), e *Buscar “…” no guia*
  busca nele o que foi digitado.
- **Mensagens.** Um problema que o programa relata nomeia o engine, o arquivo ou o recurso, com a correção ali mesmo;
  um **?** ao lado abre o item correspondente em [Solução de problemas](#solução-de-problemas).
- **Primeiros passos.** Até o primeiro torneio ser iniciado, a página inicial mostra as etapas do
  [Início rápido](#início-rápido), cada uma com seu botão, marcada quando concluída; *Ocultar* as guarda.
- [Relate um problema](#relatar-um-problema) quando o programa não fizer o que deveria.

### A paleta de comandos
<!-- en: the-command-palette 725e7934 -->

**Ctrl+K** abre a paleta de comandos: as páginas e as abas, os engines da biblioteca (ela abre a ficha), os livros,
os torneios da página inicial, as seções do guia, e as ações possíveis no momento (*Pausar*, *Retomar*, *Pausar
tudo*, *Parar*, *Iniciar*, *Adicionar à fila*, *Relançar … com…*, os itens de *Importar…*, *Verificar os não
verificados*, *Exportar para uma lista de rating…*, *Copiar* a classificação, *Copiar a versão*, *Relatar um
problema…*, as cores do tabuleiro e os conjuntos de peças, os sons dos lances ligados ou desligados…). Maiúsculas e
minúsculas não importam e as letras digitadas não precisam ser vizinhas ("lr" encontra *Lista de rating*); os nomes
que começam com elas, ou com uma palavra que começa com elas, vêm primeiro. As setas para cima e para baixo escolhem,
Enter executa, Esc fecha; Ctrl+K não é capturado enquanto se digita num campo de texto.

### Idioma, tema e tamanho
<!-- en: language-theme-and-size 954fe416 -->

A interface está em inglês, francês, alemão, espanhol, italiano, português do Brasil, russo ou chinês simplificado.
Ao iniciar, ela segue o primeiro idioma preferido do sistema (inglês quando não há tradução para ele); é possível
impor outro em **Preferências**. O chinês usa uma fonte do sistema: Microsoft YaHei no Windows, Noto Sans CJK ou
WenQuanYi no Linux. O tema é claro, a menos que o escuro, ou o do sistema, seja escolhido em **Preferências**.

As traduções em espanhol, italiano, português, russo e chinês são novas: correções de jogadores desses idiomas são
bem-vindas. As da interface são simples arquivos `.po` em `gui/i18n`, que o Poedit e programas semelhantes abrem; as
deste guia são `docs/guide.<language>.md`.

As páginas usam a janela inteira, qualquer que seja o tamanho dela: os tabuleiros da aba Ao vivo crescem com ela, o
formulário de novo torneio se divide em duas colunas numa janela larga, e a lista das páginas Engines, Livros e
Ratings pode ser alargada arrastando a sua borda direita. Tudo (texto, botões, tabuleiros) também pode ficar maior
ou menor, de 80 a 250 %: **Preferências** > Tamanho da interface, ou Ctrl + roda do mouse, Ctrl + e Ctrl -, e
Ctrl 0 para voltar a 100 %. F11 coloca a janela em tela cheia, e a tira dela.

Um indicador na aba *Ao vivo* e no botão *Torneios* dá o número de partidas em andamento. Um campo numérico aceita contas:
`40*60`, `15+10`, com `+ - * /` e parênteses, calculadas com Enter ou ao sair do campo; Esc devolve ao campo o valor
que ele tinha.

### Os tabuleiros e seus botões
<!-- en: boards-and-their-buttons 1ed997f4 -->

Sob todo tabuleiro que mostra uma partida (a reprodução da aba *Partidas*, a partida em foco em *Ao vivo*, um
tabuleiro destacado, a página Tabuleiro), uma fileira de pequenos botões vai para a posição inicial, o lance
anterior, o próximo lance e o último, e gira o tabuleiro; as dicas deles indicam as teclas, que fazem o mesmo:
← → Home End, e F para girar. Quem prefere as teclas oculta os botões com **Preferências** > *Aparência* > *Botões de
navegação sob os tabuleiros*, um clique direito sobre eles, ou a paleta de comandos (*Ocultar os botões de navegação
sob os tabuleiros*, e *Mostrar…* para trazê-los de volta). A roda do mouse sobre uma lista de lances rola a lista,
nunca os lances. **A** em qualquer tabuleiro abre a posição dele na página [Tabuleiro](#tabuleiro) e a analisa.

### Versão e novas versões
<!-- en: version-and-new-versions 70e3e9a2 -->

A versão está no título da janela e em **Preferências**, cujo *Copiar* a coloca na área de transferência junto com o
sistema, para um relatório de bug. *Avisar quando sair uma nova versão* (desligado por padrão) pergunta ao GitHub,
ao iniciar, qual é a última versão publicada, no máximo uma vez por dia e em segundo plano; quando ela é mais
recente, a página inicial avisa, com *Página de download* (a página da versão no GitHub, no navegador), *Novidades*
(as notas dela) e *Ocultar* (até a próxima versão). O programa não baixa nem instala nada, e uma falha (sem rede,
GitHub inacessível) só é registrada no log.

### Relatar um problema
<!-- en: reporting-a-problem 3e4e5989 -->

*Relatar um problema…* (em **Preferências**, na página **Log**, na paleta de comandos e na mensagem de uma falha ao
iniciar) abre uma nova issue no GitHub, no navegador, a partir do formulário de relatório de bug, já preenchida com a
versão, o sistema (sistema operacional, processador, memória, placa de vídeo e driver) e as últimas linhas do log.
Leia antes de enviar, remova o que você não quer mostrar (caminhos, nomes de engines) e diga o que você fez, o que
esperava e o que aconteceu. O programa não envia nada por conta própria. As ideias vão para o mesmo lugar: *New
issue*, *Idea*.

### Tamanho e posição da janela
<!-- en: window-size-and-place 892d41c4 -->

A janela abre como foi deixada: o tamanho, a posição nas telas, maximizada ou não. Quando a tela em que ela estava
não existe mais (um monitor desconectado), ela abre no meio da tela principal; quando o tamanho não cabe mais na tela
(uma resolução menor), no tamanho padrão. Os painéis arrastados para fora dela, como um tabuleiro destacado, também
voltam para onde estavam (`layout.ini`), e para dentro da janela quando a tela deles não existe mais. O Wayland não
deixa um programa escolher a posição da sua janela: lá, só o tamanho e o estado maximizado são restaurados.

### Renderização da janela
<!-- en: drawing-the-window caff7bef -->

A janela é desenhada com OpenGL 3.2. Onde ele não existe (uma máquina virtual sem aceleração 3D, uma área de
trabalho remota, um servidor antigo), o programa a desenha pelo renderizador do próprio SDL: Direct3D 11 no Windows,
OpenGL 2.1, ou só o processador. O log diz qual, e por que o OpenGL não foi usado; **Preferências** > Renderização
também mostra, com o nome da placa de vídeo quando é o OpenGL que desenha. *Renderização por software* ali (ou
`--software` na linha de comando, para uma única execução) faz o processador desenhar a janela a partir da próxima
inicialização, para um driver gráfico que falha ou trava. Ela usa mais tempo de processador, redesenha só o que muda
(e o que se move, como os relógios, 15 vezes por segundo em vez de 30 enquanto o mouse e o teclado estão parados),
desenha cantos retos, e os painéis do espaço de trabalho flutuam dentro da janela principal em vez de irem para
outra tela. No Wayland (e no macOS) a imagem do processador ainda é mostrada pelo driver gráfico; se não houver
nenhum, inicie o programa com `SDL_VIDEODRIVER=x11`. Quando nenhuma forma de desenhar funciona, uma mensagem avisa
antes de o programa terminar.

### Aparência
<!-- en: appearance decda352 -->

**Preferências** > *Aparência* escolhe a aparência de todos os tabuleiros do programa (o mosaico ao vivo e a partida
acompanhada, os tabuleiros destacados, a tela grande, uma partida reproduzida em *Partidas*, a aba *Aberturas*, as
aberturas de um livro), com um pequeno tabuleiro ao lado das opções que as mostra:

![Preferências > Aparência: o tabuleiro marrom e as peças Celtic](screenshots/appearance.png)

- **Tabuleiro**: as cores das casas, para o tema claro e para o tema escuro (é desenhado o tabuleiro do tema em
  uso). Uma predefinição (*Cinza-azulado*, as cores das versões anteriores; *Marrom*; *Azul*; *Verde*; *Cinza*), ou
  *Personalizado*: um clique em *Casas claras*, *Casas escuras* ou *Último lance* abre um seletor de cores, e alterar
  uma cor torna o tabuleiro personalizado a partir das cores que ele tinha. *Último lance* é sobreposto às duas casas
  do último lance, com uma transparência que deixa passar a cor da casa.
- **Peças**: um conjunto embutido no programa (*Cburnett*, o padrão; *Chessnut*; *Celtic*; *Fantasy*; os autores e
  as licenças na dica e em `ATTRIBUTION.md`), ou *Sua própria pasta…*: uma pasta de 12 imagens SVG chamadas
  `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` e `bK.svg` … `bP.svg` (rei, dama, torre, bispo, cavalo e
  peão brancos e pretos). As imagens são desenhadas com o nanosvg, como as embutidas: formas simples, preenchimentos,
  contornos e gradientes; sem texto, filtros nem imagens incorporadas. Quando um arquivo falta, tem mais de 1 MB ou
  não é uma imagem que possa ser desenhada, a página diz qual e por quê, as peças padrão são desenhadas enquanto
  isso, e *Ler de novo* relê a pasta depois que os arquivos forem corrigidos. A página web da transmissão mantém as
  peças padrão.
- **Sons dos lances** (desligados por padrão): *Um som para cada lance* toca uma batida curta a cada lance da partida
  acompanhada, em *Ao vivo* e na tela grande, e uma mais pesada para uma captura; não para cada partida do mosaico.
  *Volume*, e *Testar um lance* / *Testar uma captura* para ouvi-los. Os sons são gerados pelo programa (nenhum
  arquivo de som); o dispositivo de som só é aberto enquanto eles estão ligados, e quando não há nenhum o log diz por
  quê e o programa fica em silêncio.

- **Figuras na notação** (ligadas por padrão): os lances da página Tabuleiro, nos lances dela e nas linhas da sua
  análise, são escritos com as peças do conjunto escolhido acima (Nf3 vira um cavalo e f3); desligadas, com letras.

A paleta de comandos também tem essas opções (*Cores do tabuleiro: Marrom*, *Peças: Celtic*, *Ligar os sons dos
lances*…). Elas são guardadas em `settings.json` no perfil; no modo portátil, a pasta de peças é guardada em relação
à pasta do programa.

**Logos dos engines**: a ficha de um engine na página **Engines** tem *Logo* > *Escolher…*, uma imagem PNG, JPEG ou
SVG de no máximo 2 MB (no máximo 4096 pixels em cada direção), mostrada ao lado do nome do engine em *Ao vivo*, na
tela grande e na página web da transmissão. A imagem é copiada para a pasta `logos` do perfil, de modo que o original
pode ser movido ou apagado e o perfil movido inteiro; *Remover* a retira (e a cópia dela, a menos que outro engine a
use). O logo pode ser trocado enquanto o engine joga.

### O tabuleiro 3D
<!-- en: the-3d-board cc0dd596 -->

**Preferências** > *Aparência* > *Tabuleiro 3D* > *O tabuleiro da página Tabuleiro em 3D* desenha o tabuleiro da
página [Tabuleiro](#tabuleiro) em três dimensões, como a fotografia de um tabuleiro de verdade (desativado por padrão;
todos os outros tabuleiros continuam planos). A placa de vídeo o calcula por path tracing: a imagem fica mais nítida
por alguns segundos enquanto nada se move, alguns milissegundos da placa a cada quadro, e depois a placa descansa;
enquanto o tabuleiro gira, uma imagem mais grosseira fica no lugar. O pequeno tabuleiro de *Aparência* passa então a
ser desenhado em 3D também, para que as escolhas ao lado apareçam à medida que mudam:

- a iluminação, como os fotógrafos iluminam uma natureza-morta: *Estúdio*, *Low key*, *High key*, *Showroom*, *Luz de
  janela*, *Hora dourada* ou *Salão de torneio*;
- *Exposição*: mais claro ou mais escuro do que a iluminação deixa, em stops, cada um dobrando ou dividindo pela
  metade a luz;
- *Qualidade*: as amostras de que a imagem é feita antes de descansar, 512 por padrão; menos poupam o gráfico de um
  processador durante os torneios;
- *Verniz*, de uma madeira encerada e fosca a uma camada em que as luzes aparecem, e *Brilho*, do acetinado ao
  espelho;
- *Padrão* devolve a exposição, a qualidade, o verniz e o brilho ao que eram; a iluminação fica.

O que o tabuleiro plano mostra está nele: a barra de avaliação, o último lance, a peça pegada e para onde ela pode ir,
um rei em xeque, as setas da análise, que aparecem de leve através das peças, a resposta que o engine que jogou o lance
esperava, tracejada, o julgamento do lance como selo acima da sua casa e a moldura de uma linha mostrada no lugar da
partida. Um clique esquerdo escolhe uma casa como no tabuleiro plano, numa peça ou no tabuleiro atrás dela, e uma peça
pode ser arrastada, com o fantasma dela seguindo o ponteiro (veja [Tabuleiro](#tabuleiro)). Se você mantiver o botão
direito pressionado enquanto move o mouse, a câmera gira em volta do tabuleiro (um clique direito sem mover continua
soltando a peça pegada), a roda aproxima, e um clique da roda traz a vista de volta; girar o tabuleiro (F) faz a câmera
dar meia-volta.

O tabuleiro 3D precisa do OpenGL 3.3. Onde a janela é desenhada sem ele (o renderizador por software de
[Renderização da janela](#renderização-da-janela), algumas máquinas virtuais e áreas de trabalho remotas), os
tabuleiros continuam planos e *Aparência* diz por quê. As peças são as Staunton-Pieces de clarkerubber, sob a licença
MIT; *Aparência* tem o link para elas.

## Atalhos de teclado
<!-- en: keyboard-shortcuts e0d74f26 -->

| Teclas | Onde | O que fazem |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Em todo lugar | A paleta de comandos |
| <kbd>F1</kbd> | Em todo lugar | Este guia, na seção do que está sob o mouse; de novo: fecha-o |
| <kbd>F11</kbd> | Em todo lugar | Tela cheia, e volta (a janela própria de um tabuleiro destacado quando ele tem o teclado) |
| <kbd>Ctrl</kbd>+roda, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Em todo lugar | Tamanho da interface; <kbd>Ctrl</kbd>+<kbd>0</kbd>: volta a 100 % |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Um campo numérico | Calcular o valor; devolver ao campo o valor que ele tinha |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | O painel do guia | Voltar, avançar |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | O painel do guia | Buscar no guia |
| <kbd>Esc</kbd> | O painel do guia | Fechá-lo |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Page Up</kbd> <kbd>Page Down</kbd> | A paleta de comandos | Escolher |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | A paleta de comandos | Executar, fechar |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Home</kbd> <kbd>End</kbd> | Um tabuleiro que mostra uma partida | Lance anterior, próximo lance, início, último lance |
| <kbd>F</kbd> | Um tabuleiro | Girá-lo |
| <kbd>A</kbd> | Um tabuleiro | Abrir a posição na página Tabuleiro e analisá-la; na página Tabuleiro: iniciar ou parar a análise |
| <kbd>End</kbd> | A partida em foco em *Ao vivo* | Voltar ao lance atual |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | A página Tabuleiro | Colar um FEN, uma linha EPD, um PGN ou uma lista de lances |
| <kbd>G</kbd> | A página Tabuleiro | Analisar a partida, passada após passada; de novo: parar |
| <kbd>X</kbd> | A página Tabuleiro | Mostrar ou ocultar a ameaça |
| <kbd>N</kbd>, <kbd>Shift</kbd>+<kbd>N</kbd> | A página Tabuleiro | O próximo, o anterior lance marcado pela análise |
| <kbd>Esc</kbd> | A página Tabuleiro | A partida de volta depois que uma linha foi mostrada; soltar a peça levantada |
| <kbd>←</kbd> <kbd>→</kbd> | A tela grande | A partida anterior ou a seguinte |
| <kbd>+</kbd> <kbd>-</kbd> | A tela grande | Texto maior ou menor |
| <kbd>Esc</kbd> | A tela grande | Sair dela |
| <kbd>Tab</kbd>, setas, <kbd>Enter</kbd>, <kbd>Espaço</kbd> | Novo torneio | Passar entre as linhas, listas e botões; abrir uma linha ou uma lista |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | Os participantes | Mandar os participantes selecionados para o topo, para o fim |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Os participantes | Movê-los uma posição |
| <kbd>Delete</kbd> | Os participantes | Removê-los |
| <kbd>Menu</kbd>, <kbd>Shift</kbd>+<kbd>F10</kbd> | Os participantes | Abrir o menu do participante (as configurações próprias dele) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | A biblioteca da configuração | Ir para o filtro dela |
| <kbd>Enter</kbd> | A biblioteca da configuração | Adicionar os engines selecionados, ou o primeiro mostrado |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | A biblioteca da configuração | Selecionar todos os engines mostrados |
| <kbd>Ctrl</kbd>+clique, <kbd>Shift</kbd>+clique | Listas | Selecionar vários |
| <kbd>Delete</kbd> | A página Engines | Remover o engine selecionado (após uma confirmação) |
| <kbd>Enter</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | O console de um engine | Enviar a linha; trazer de volta as linhas anteriores |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | O modo headless | Parar depois das partidas em andamento |

## A página inicial
<!-- en: the-home-page 40031a16 -->

A página **Torneios** é uma sala de controle: o que está rodando e o que precisa de você vem primeiro, depois o resto.

- **Primeiros passos**, até o primeiro torneio ser iniciado: as etapas do [Início rápido](#início-rápido) (adicionar
  engines, configurar um torneio, iniciá-lo), cada uma com seu botão e marcada quando concluída, e *Ler o início
  rápido*, que o abre no painel do guia. *Ocultar* guarda o cartão.
- **Pausar tudo**, ao lado de *Abrir uma pasta…* e *Novo torneio*, pausa os torneios em andamento (nenhum novo par de
  partidas começa, as partidas em andamento vão até o fim) e segura a fila: nenhum torneio da fila começa até
  **Retomar tudo**, que os retoma e deixa a fila seguir, imediatamente se um torneio terminou nesse meio-tempo.
- **Núcleos**: as threads de hardware da máquina, e as que as partidas em andamento ocupam, o resto livre. Uma
  partida ocupa as `Threads` do seu engine que tem mais, já que os dois engines pensam alternadamente; a barra mostra
  o nome de cada torneio e a sua contagem, o cursor os detalhes.
- **Requer atenção**, quando algo requer, cada item com a correção ali mesmo: um engine que não consegue iniciar (veja
  [Um engine que não consegue iniciar](#um-engine-que-não-consegue-iniciar): *Tentar novamente*, *Retirar…*,
  *Substituir…*, *Log do engine*); o torneio que um erro ou um engine parou, com o que o parou, *Abrir o engine*,
  *Abrir o log*, *Retomar*, *Abrir* e *Ocultar*; um disco cheio demais para uma noite de partidas (menos de 1 GB
  livre, ou menos de 2 % de um disco de menos de 500 GB), o da pasta dos torneios ou o de um torneio em andamento,
  verificado a cada minuto, com *Abrir a pasta* e *Alterar a pasta dos torneios…* (para onde vão os novos torneios,
  como em **Preferências**).
- **Em andamento**: um cartão para cada torneio em andamento: o nome, o formato, o controle de tempo e o estado, uma
  barra de progresso com as partidas jogadas das previstas e as partidas em andamento, quando ele termina, e quem
  lidera: o primeiro da classificação com seu rating contra o conjunto dos adversários e seus pontos (um mata-mata:
  os jogadores restantes; um match por equipes: o placar das equipes e o Elo correspondente), o engine à frente num
  match com seu Elo e o placar, ou o LLR de um SPRT com seus limites, depois o Elo e o placar. *Abrir* vai para o
  espaço de trabalho dele, *Pausar* o pausa. Um torneio que pode ser retomado aparece em vez disso em *Interrompido*,
  com *Retomar*. Veja [Vários torneios ao mesmo tempo](#vários-torneios-ao-mesmo-tempo) para os pesos e os núcleos de
  cada um.
- **Fila**: os torneios em espera, cada um com sua duração e quando começa, e quando tudo termina (veja
  [Fila e relançamento](#fila-e-relançamento)).
- **Últimos concluídos**: os cinco últimos torneios concluídos, com o resultado: o placar de um match, a decisão do
  SPRT, o vencedor de um mata-mata (da final), o placar de um match por equipes, ou o primeiro da classificação com
  seus pontos sobre o número de partidas. *Relançar com…* para um match, *Clonar* (veja [Fila e
  relançamento](#fila-e-relançamento)) e *Abrir*.
- **Saúde**: os crashes ou travamentos, as perdas por tempo e os lances ilegais das últimas 24 horas, a memória livre
  e instalada, e os processos de engines em execução; depois os engines que perderam essas partidas, os três com
  mais, com *Abrir o engine* e *Abrir a pasta* (a pasta `logs` do torneio guarda o log do engine de cada partida que
  terminou mal). Uma perda por tempo antes do lance 20 é contada à parte: ela vem mais vezes de um engine mal
  configurado (threads demais, um hash grande demais para alocar a tempo, uma rede carregada a cada partida, um
  início lento) do que da sua gestão do tempo.
- **Todos os torneios**: todos os torneios do perfil, os mais recentes primeiro, com *Abrir*, *Clonar* e *Pasta*; um
  clique no título recolhe o cartão. O resultado de um torneio concluído aparece ao passar o cursor sobre o estado dele.

A paleta de comandos também tem estas ações: *Pausar tudo*, *Retomar tudo*, *Relançar … com…* para os últimos
matches concluídos, *Clonar …* para o torneio mostrado e os últimos concluídos, *Abrir a pasta dos torneios* e
*Alterar a pasta dos torneios…*.

## Engines
<!-- en: engines 4f15155a -->

A página **Engines** é a biblioteca de engines: a lista à esquerda, a ficha do engine escolhido à direita.

### Adicionar e verificar engines
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Adicionar…** registra um executável UCI. O engine é iniciado uma vez para ler o nome, o autor e as opções. As
opções são editadas numa tabela: os valores alterados ficam destacados, e só os valores diferentes do padrão do
engine são enviados com `setoption` no início de cada partida. `Threads` e `Hash` vêm primeiro.

- **Duplicar** cria uma segunda entrada para o mesmo binário, com outras opções (um `Hash` diferente, um nível de
  força, outro arquivo de rede…). As entradas recebem o nome do `id name` UCI quando são adicionadas e podem ser
  renomeadas.
- **Verificar** inicia o engine, mede a rapidez com que ele responde a `uci` e `isready`, e o faz jogar um lance;
  depois executa as etapas de conformidade UCI do fastchess (`id name` e `id author`, posições a partir de um FEN e
  com lances, `go` com cada relógio, linhas info com uma pontuação inteira…), até a primeira que falhar. O resultado
  (✓ ou o erro) fica ao lado do engine com a data, e *Relatório de conformidade UCI* lista as etapas, com o que o
  engine escreveu depois da que falhou. Um engine com quem se fala em xboard só passa pela primeira parte.
- **Console…** conversa com o engine, fora de qualquer torneio: ele é iniciado com o programa, os argumentos e a
  pasta de trabalho da biblioteca, e um engine UCI recebe `uci` e depois os valores das opções da biblioteca. As
  linhas digitadas são enviadas como estão (Enter; as setas para cima e para baixo trazem de volta as anteriores), e o
  log mostra cada linha com a hora, as enviadas marcadas com `>` e a saída de erro com `!`. Botões enviam os comandos
  habituais (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; os do WinBoard para um engine
  WinBoard, com quem se fala no próprio protocolo dele, a menos que *Pelo adaptador* esteja marcado) e a posição da
  página Tabuleiro. *Copiar* e *Salvar…* guardam o log. O console aparece no lugar das configurações do engine
  (*Preferências* volta a elas, com o console continuando aberto); *Fechar* encerra o engine. Enquanto um torneio
  roda, o que o engine calcula tira núcleos das partidas dele: o console avisa. O engine do console não é um dos
  processos do torneio: o fim de um torneio não o encerra.
- **Teste rápido** joga um match curto contra o próprio engine ou outro: 10 partidas em 10+0.1 a partir de 5
  posições (uma com captura en passant), pelo mesmo código de um torneio, mostrado no seu espaço de trabalho enquanto
  é jogado. O resultado fica ao lado do engine: o placar, as perdas por tempo, os crashes e os lances ilegais, e a
  parcela do relógio que ele usou, que revela um engine que joga rápido demais ou perto demais da queda da bandeira.
  As PVs de mate dele são verificadas (veja *Verificar as PVs de mate* em [PGN e outros](#pgn-e-outros)). Só o último
  teste rápido é guardado, em `tests/` no perfil (com os logs dos engines das partidas dele que terminaram mal).

### Uma biblioteca grande
<!-- en: a-large-library beb231ed -->

- **Grupos**: rótulos livres separados por vírgulas (`pool A`, `list 40/15`…). A lista pode ser filtrada por grupo
  ou pesquisada, e um grupo pode ser adicionado a um torneio com um clique. *Notas*: qualquer coisa a lembrar sobre o
  engine. *Logo*: uma imagem mostrada ao lado do nome dele em *Ao vivo*, na tela grande e na página web da transmissão
  (veja [Aparência](#aparência)).
- **Busca e filtros**: toda busca de engines (a biblioteca, e as listas de engines das outras páginas) encontra um
  nome, o nome que o engine informa, o autor, o caminho do programa ou as notas; o filtro de estado mostra os engines
  que funcionam, que falharam ou mudaram, que nunca foram verificados, que perderam o programa, que falam WinBoard, ou
  que estão inativos. Ctrl ou Shift clique seleciona vários engines: *Verificar* (um depois do outro, nunca enquanto
  um torneio é jogado), *Adicionar a um grupo…*, *Remover…*. *Mais…* verifica os nunca verificados (*Verificar os não
  verificados*) e remove os programas ausentes. Uma opção de texto que contém um arquivo ou uma pasta (`EvalFile`,
  `SyzygyPath`…) tem um botão *…*.
- **Inativo** (na ficha, ou pelo clique direito para todos os engines selecionados): o engine fica na biblioteca,
  acinzentado, mas fora dos seletores de engines (configuração, suítes, teste rápido, relançar, adicionar um engine a
  um torneio em andamento) e de *Verificar os não verificados*. Um torneio ou uma configuração que já o tem o mantém.
- **Usa uma GPU** (na ficha): um engine de rede neural que joga numa placa de vídeo, como o Lc0. Não jogam ao mesmo
  tempo mais engines desses do que as *GPUs* de **Preferências**, num torneio e entre torneios (veja [Vários torneios
  ao mesmo tempo](#vários-torneios-ao-mesmo-tempo)); vale a partir do próximo torneio de que ele participar.
- **Elo de referência**: o rating do engine numa lista em que você confia. Um torneio ou a lista de rating podem ser
  ancorados nele, e as partidas de um torneio o levam nas tags `WhiteElo` e `BlackElo` (copiado quando o engine entra
  no torneio). *Importar…* > *Uma lista de rating, para o Elo de referência…* o define para muitos engines de uma vez
  (veja [Importar engines e ratings](#importar-engines-e-ratings)).

### Iniciar engines
<!-- en: starting-engines 09d107ba -->

- **A leitura das opções** nunca bloqueia um início. *Retomar* começa imediatamente: um torneio joga com os engines
  copiados na pasta dele. *Iniciar*, a fila e o teste rápido só esperam a leitura de um engine que vão jogar, lido
  primeiro; qualquer outra leitura ou verificação é interrompida e refeita mais tarde. Um início que espera diz isso
  no botão (*Aguardando …*, com *Cancelar*), e outro início é recusado enquanto isso. Um início também espera uma
  suíte de testes em execução, e uma suíte de testes não começa enquanto o teste rápido roda ou um início espera.
  Leituras, verificações e suítes de testes rodam ao lado dos torneios, que jogam em processos próprios. Um engine que
  aguarda a leitura pode ser lido em seguida pelo menu dele.
- *Reiniciar o engine a cada partida* inicia um novo processo para cada partida em vez de reutilizá-lo. Do contrário,
  um torneio mantém um processo em espera por engine que ainda tem partidas a jogar, até 32 (mais dois por partida em
  andamento), e fecha um engine depois da última partida dele: num mata-mata assim que ele perde o match, num suíço
  quando não lhe resta nenhuma partida na última rodada.
- *Tempo limite de início (s)* (60 por padrão) é o tempo dado a um engine para responder `uciok` quando ele inicia e
  `isready` no início de cada partida; um engine lento para iniciar (uma rede grande a carregar) precisa de mais. Um
  engine que não consegue iniciar é tentado de novo três vezes, com 10 s de intervalo, com o mesmo tempo limite; uma
  pausa suspende as tentativas, sem gastá-las. Depois as partidas dele são postas de lado e as outras continuam (veja
  [Um engine que não consegue iniciar](#um-engine-que-não-consegue-iniciar)). Um torneio criado depois copia o tempo
  limite para os participantes dele.

### Programas recompilados
<!-- en: rebuilt-programs 549daa6e -->

- **Programas movidos ou recompilados**: a biblioteca se lembra de cada programa (tamanho, data, CRC32). Um programa
  que não está mais lá é marcado com **!**, com *Localizar…*, e um torneio não começa com ele. Um programa que mudou
  (uma nova compilação, ou outro arquivo no mesmo caminho) tem as opções lidas de novo automaticamente.
- **O que uma nova compilação mudou**: quando a nova leitura difere da anterior, o engine é marcado com **Δ** e a
  ficha dele lista as diferenças até *Fechar*: o novo `id name` (com *Renomeá-lo* quando o engine tinha o nome do
  antigo) e o autor, cada opção cujo padrão, limites ou escolhas mudaram (antes e agora), as opções novas e as que
  sumiram, e o seu valor de cada uma (o valor de uma opção que sumiu é descartado; um que a nova compilação não aceita
  mais fica em laranja). Para uma opção cujo padrão mudou, ou cujo valor não cabe mais, escolha *Manter o meu valor*
  (ou *Manter o padrão antigo* onde você não tinha nenhum: joga como antes) ou *Usar o novo padrão*; *Manter os meus
  valores* e *Usar os novos padrões* escolhem para todas. O aviso some quando todas as escolhas forem feitas, a menos
  que tenha mais a dizer. Várias compilações antes de ele ser fechado formam um único aviso, comparado com a leitura
  anterior à primeira. *Com falha ou alterado* no filtro de estado também mostra esses engines, e a paleta de comandos
  tem *O que mudou*, *Manter os meus valores*, *Usar os novos padrões* e *Fechar o aviso de mudanças* para cada um.
- **Os programas dos torneios**: cada torneio registra os programas com que foi iniciado. O resumo da configuração
  avisa de um engine cujo programa mudou desde o último torneio dele (o mais recente da pasta dos torneios que jogou
  esse programa), com *O que mudou* quando a ficha dele tem um aviso. Ao retomar um torneio com um programa
  recompilado ou ausente desde então, o cabeçalho dele avisa, com *Continuar* (o aviso fecha) e *Pausar*; o torneio
  não para sozinho.

### Importar engines e ratings
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importar…** > *Uma lista de engines…* lê a lista de engines de outro programa: ChessGUI (`ChsGUI_Eng.TXT`, com as
opções do `ChsGUI_Cfg.ini` ao lado), cutechess e Banksia (`engines.json`), Arena (`ArenaENG.cfg`, na pasta do Arena:
o programa, os argumentos, o protocolo de cada engine e, para os engines UCI, os valores de opções definidos no
Arena) e outras listas no estilo INI. Uma lista que não fornece nenhum engine avisa, com os formatos lidos e *Todos os
engines de uma pasta…* para tentar em vez disso.

*Todos os engines de uma pasta…* traz todos os programas encontrados numa pasta, e se lembra da pasta: *Atualizar: os
engines novos da pasta* passa então a oferecer só os engines adicionados a ela desde então. Os engines encontrados
são listados primeiro, para desmarcar os que ficam de fora; os engines que já estão na biblioteca (mesmo programa e
argumentos) são pulados; as opções dos outros são lidas; os engines WinBoard também são importados.

*Importar…* > *Uma lista de rating, para o Elo de referência…* lê uma lista de rating, colada ou aberta: a versão em
texto de uma lista de rating ou a página web dela (salva ou copiada), a saída do Ordo (texto ou CSV), ou um CSV de
linhas `name,elo`. Um nome da lista é o engine da biblioteca com o mesmo nome, ou o único cujo nome é o mesmo sem
contar maiúsculas e espaços (como para os jogadores dos arquivos PGN importados, veja [Ratings](#ratings)). A prévia
mostra o Elo de referência de cada engine, o antigo e o novo, marcado para ser definido, e os nomes da lista que não
são nenhum engine da biblioteca. Os nomes de uma lista muitas vezes trazem a compilação (`64-bit 4CPU`): renomeie os
engines para que correspondam a eles, se precisar.

### Engines WinBoard
<!-- en: winboard-engines 079e4849 -->

**Os engines WinBoard** (protocolo xboard, CECP) jogam como os outros. O *Protocolo* de um engine é detectado quando
ele é adicionado (*Automático*), ou escolhido: UCI ou WinBoard. O fastchess só fala UCI: um engine WinBoard é
iniciado por uma tradução embutida no fastchess-gui (`fastchess-gui --wb2uci ENGINE [ARGS]`, que o fastchess inicia
no lugar do engine), e as opções dele, `memory`, `cores` e o caminho Syzygy viram opções UCI. Ela conduz o engine
como o WinBoard faz: features do protocolo 2, ou protocolo 1 para os engines que não enviam nenhuma; posições com
`setboard`, ou modo de edição; lances em coordenadas ou SAN; `level`, `time`/`otim`, `st`, `sd`, `nps`; saída de
pensamento, abandono. Os engines marcados com ↔ na lista são engines WinBoard.

- Um engine de protocolo 1 sem `setboard` nem modo de edição (o TSCP, por exemplo) só pode começar da posição
  inicial: dê a ele aberturas como lances (um livro PGN), não como posições (EPD).
- Chess960 para um engine com a variante `fischerandom`: `variant fischerandom`, posições em X-FEN, roque como
  `O-O`/`O-O-O`, como o WinBoard os envia.
- *Scores from White's side*, entre as opções do engine: para os engines antigos que dão as pontuações do ponto de
  vista das Brancas em vez do lado que tem a vez, como o protocolo pede; as pontuações deles com as Pretas são
  invertidas.
- Um engine que pede para ser iniciado de novo a cada partida (`feature reuse=0`) recebe *Reiniciar o engine a cada
  partida* marcado quando é adicionado.
- `FCG_WB2UCI_LOG=FILE` no ambiente grava cada linha trocada com os engines WinBoard em `FILE`.

## Livros
<!-- en: books 4cf13005 -->

**Adicionar…** registra um livro de aberturas (EPD, PGN ou Polyglot `.bin`) com um nome e uma descrição; para um
livro PGN, o número de meios-lances mantidos, para um livro Polyglot, a profundidade até a qual as linhas dele são
seguidas. A seção *Aberturas* da configuração passa então a oferecer os livros da biblioteca pelo nome, com o número
de aberturas (qualquer outro arquivo ainda pode ser escolhido). *Remover* tira um livro da biblioteca e não mexe no
arquivo.

### Verificar um livro
<!-- en: checking-a-book 96eee229 -->

O livro escolhido é lido em segundo plano, com os próprios leitores do fastchess (assim as aberturas dele são
exatamente as que um torneio joga), e verificado:

- **posições ilegais**: sem exatamente um rei de cada cor, peões na primeira ou na última fileira, o lado que não tem
  a vez em xeque;
- **duplicadas**: a mesma posição de uma abertura anterior, inclusive por transposição;
- **cores invertidas**: uma abertura anterior com as cores trocadas (um livro feito para ser jogado duas vezes, com
  as cores invertidas, não precisa das duas);
- **partidas já terminadas**: nenhum lance legal, ou material insuficiente para dar mate.

O fastchess deixa fora de um torneio as aberturas cuja posição não pode ser jogada (uma linha FEN ou EPD sem rei ou
malformada de outro jeito, um rei que poderia ser capturado, uma partida PGN cuja tag FEN é inválida), com um aviso
por livro; elas são contadas aqui como posições ilegais. Uma partida PGN cujos lances deixam de ser interpretados (um
lance malformado ou ambíguo) mantém os lances anteriores. As linhas de um livro PGN que começam com `%` (linhas de
escape, que o padrão PGN torna comentários; os livros AVT têm algumas) são ignoradas, também pelos torneios, e a
página Livros as conta (*linhas começando com %, ignoradas*).

As aberturas dele são listadas abaixo, todas ou só as que têm um problema, pesquisáveis por parte de um FEN ou por
lances (um livro PGN mostra os lances em SAN); um clique mostra a posição num tabuleiro. *Usar no próximo torneio* o
coloca na configuração (com Chess960 para um livro Chess960). *Salvar uma cópia limpa…* grava um livro EPD sem as
posições ilegais, as duplicadas e as partidas terminadas (e, se marcado, as posições com cores invertidas), e o
adiciona à biblioteca. *Rodar como suíte de testes…* abre um livro EPD em **Suítes**.

### Criar livros
<!-- en: creating-books b9c072ab -->

**Criar…** gera livros para a biblioteca (também na paleta de comandos):

- *Gerar um livro Chess324*: o Chess324 de Larry Kaufman. Os reis e as torres ficam nas casas habituais, então o
  roque é o habitual; a dama, os bispos e os cavalos de cada lado são embaralhados nas outras casas da fileira inicial
  dele (as colunas b, c, d, f e g), os bispos em casas de cores opostas. Isso dá 18 arranjos por lado, os das Brancas
  e os das Pretas escolhidos de forma independente: *As 324 posições*, ou *Só as 18 simétricas*, em que as peças das
  Pretas ficam nas mesmas colunas que as das Brancas. São posições comuns, gravadas num arquivo EPD (oferecido ao lado
  dos livros da biblioteca, ou em `books/` no perfil) e jogadas por qualquer engine, sem Chess960.
- *Construir um livro Polyglot…*: um livro Polyglot (`.bin`) a partir das partidas de um arquivo PGN, como faz o
  `make-book` do Polyglot, com a conversão que transforma os livros PGN dos participantes em livros Polyglot. O
  formulário dele, no lugar dos detalhes do livro, recebe o arquivo PGN (o livro PGN escolhido na lista, se houver), a
  **profundidade** (os meios-lances de cada partida que entram no livro), o **mínimo de partidas** de que um lance
  precisa na sua posição para ser mantido (o `-min-game` do Polyglot; os outros ficam de fora por serem jogados
  raramente demais), os **pesos**, *por pontos* (os pontos do lado que jogou o lance: vitória 2, empate 1, derrota 0,
  como o Polyglot conta; as partidas sem resultado ficam de fora, assim como um lance cujas partidas foram todas
  perdidas) ou *por número* (o número de partidas que o jogam), e, se marcado, um **Elo**: só contam os lances de um
  jogador com rating de pelo menos esse valor, pela tag `WhiteElo` ou `BlackElo` da partida (um jogador sem ela fica
  de fora). *Construir…* pergunta onde gravar o livro (ao lado do arquivo PGN por padrão), depois lê as partidas em
  segundo plano, com o progresso e *Cancelar*, ao lado do formulário e no fim da lista; o livro, ordenado por chave
  como o formato exige, é então adicionado à biblioteca, com as opções na descrição. O console diz quantas partidas
  entraram nele, quantas as opções deixaram de fora e quantas não puderam ser lidas.

## Tabuleiro
<!-- en: board 0bbd00c9 -->

A página Tabuleiro mostra uma partida ou uma posição, vinda de qualquer lugar: **A** em qualquer tabuleiro do programa
(uma partida do mosaico da aba *Ao vivo* ou a acompanhada, o tabuleiro da aba *Partidas*, o da aba *Aberturas*, o de uma
abertura de um livro) abre a posição dele aqui e a analisa; na página Tabuleiro, A inicia e para a análise. *Voltar ao
torneio* (ou *Voltar*) retorna à página de onde se veio. O tabuleiro dela pode ser desenhado em 3D: veja [O tabuleiro
3D](#o-tabuleiro-3d).

O tabuleiro fica à esquerda, com os jogadores acima e abaixo dele; à direita, de cima para baixo, a análise, os lances
(com a tabela dos lances, a revisão da partida e as partidas do arquivo em abas) e o gráfico da partida. O subtítulo
da página diz que partida é: o evento e a rodada, o resultado, o controle de tempo, o código ECO e a abertura, e como
ela terminou.

Um lance é feito no tabuleiro com o mouse: um clique na peça e depois outro na casa dela, ou a peça arrastada até lá.
Enquanto é arrastada, ela segue o ponteiro e a casa onde cairia fica destacada; solta em outro lugar, ela volta, ainda
selecionada, para ser movida com um clique. No tabuleiro 3D, um fantasma translúcido da peça segue o ponteiro, flutuando
sobre o tabuleiro e passando por trás das peças à frente dele, enquanto a imagem fica como está; a peça em si se move
quando é solta.

### Abrir, colar e montar uma posição
<!-- en: opening-pasting-and-setting-up-a-position 3778ddb5 -->

- **Abrir um PGN…** lê qualquer arquivo PGN (um arquivo também pode ser solto sobre a janela), em segundo plano com
  uma barra de progresso: um arquivo de milhões de partidas leva alguns segundos, e as partidas dele são listadas numa
  aba ao lado dos lances, filtradas pelo que é digitado (cada palavra encontrada nos jogadores, no evento, no local,
  na data, na rodada, na abertura, no código ECO ou no resultado). Um clique mostra uma partida. As avaliações dos
  lances desenham o gráfico: são lidos os comentários do cutechess e do fastchess (`{+0.35/18 1.2s}`) e o
  `[%eval 0.35]` do lichess e do ChessBase (do ponto de vista das Brancas, `#-3` para um mate), e também os relógios
  de `[%clk 0:01:23]`. As variantes dela são mantidas (veja [Variantes](#variantes)). Uma partida cuja tag FEN não
  pode ser jogada é recusada, e os lances de uma partida param no primeiro que não pode ser lido.
- **Colar** (Ctrl+V na página) lê a área de transferência: um FEN, ou os quatro campos de uma linha EPD; um PGN de
  uma ou mais partidas (listadas como as de um arquivo); uma lista de lances a partir da posição inicial, em SAN com
  ou sem números (`1. e4 e5 2. Nf3`) ou em UCI (`e2e4 e7e5`). Uma posição que não pode ser jogada é recusada com o
  motivo (um rei faltando, um peão na primeira fileira, o lado que não tem a vez em xeque). Direitos de roque cujo rei
  ou torre não está na casa habitual, ou escritos como colunas (`HAha`), a tornam uma posição Chess960. Um arquivo
  `.fen` ou `.epd` também pode ser solto sobre a janela.
- **Editar a posição** monta uma posição no tabuleiro: um clique coloca a peça escolhida (um segundo clique a
  retira), um clique direito a retira; o lado que tem a vez, o número do lance, os direitos de roque (oferecidos só
  quando o rei e a torre estão onde precisam estar), a casa en passant (só atrás de um peão que acabou de avançar duas
  casas) e Chess960. O FEN abaixo acompanha o tabuleiro, e um FEN digitado ali o define. *Pronto* é oferecido assim
  que a posição pode ser jogada, e a coloca no tabuleiro.
- *Copiar PGN* (à direita das abas dos lances) copia a partida mostrada, com as tags, os lances (as pontuações dos
  engines como comentários, quando ela as tem) e as variantes.
- **O tabuleiro**: os botões sob ele e ← → Home End percorrem os lances, e F (ou o último botão) o gira; um clique
  numa peça e numa casa joga um lance (um peão que promove pergunta a peça). *Copiar FEN* copia a posição.

### Os jogadores
<!-- en: the-players 1f4f551f -->

Acima e abaixo do tabuleiro, quando a partida tem jogadores: o nome de cada um, a avaliação que o engine dele deu ao
seu último lance (a pontuação das Brancas e a profundidade, numa etiqueta), o lance que ele jogou e o tempo que levou,
ou *a jogar*, e à direita o relógio dele depois do último lance, enquadrado enquanto ele tem a vez. Os relógios são os
do `[%clk]` dos lances quando a partida os tem; senão, são reconstruídos a partir da tag `TimeControl` dela (`300+2`,
`10+0.1`: segundos e incremento) e do tempo de cada lance, os lances de livro não gastando nenhum.

### A análise
<!-- en: the-analysis 1e6120de -->

Um engine da biblioteca analisa a posição mostrada e acompanha o tabuleiro. O cartão mostra o engine dela, depois um
botão com *Threads*, *Hash* (0: o do próprio engine) e *Linhas* (MultiPV), que um clique abre: as configurações
próprias da análise, guardadas para a próxima vez, que não alteram nem o engine da biblioteca nem nenhum torneio (os
outros valores de opções da biblioteca são enviados como uma partida os envia). À direita, as threads que a página
Tabuleiro usa, entre as da máquina, na cor de aviso enquanto um torneio roda (a dica diz quantas threads as partidas
do torneio ocupam: a análise compete com elas).

A busca aparece em quadros (profundidade e seldepth, nós, velocidade, tempo, acessos às tablebases, quanto o hash está
cheio), as chances de vitória, empate e derrota quando o engine as fornece (`UCI_ShowWDL`), e cada linha: a pontuação
(do ponto de vista das Brancas, a da melhor destacada), a profundidade e os lances em duas linhas, com figurinhas
(veja [Aparência](#aparência)). Passar o mouse sobre um lance de uma linha mostra no tabuleiro, enquadrado, a posição
depois dele; um clique joga a linha até esse lance. O primeiro lance de cada linha é desenhado como uma seta, o da
melhor mais escuro (**Preferências** > *Aparência* > *As setas da análise na página Tabuleiro* as desativa; as linhas
continuam mostrando os lances).

**Ameaça** (X) faz outro processo do engine analisar a posição com a vez passada: o melhor lance do adversário, se ele
jogasse de novo, desenhado como uma seta vermelha e escrito sob as linhas (nenhum enquanto o lado que tem a vez está
em xeque).

A análise espera enquanto outra página é mostrada, e continua quando a página Tabuleiro volta; *Parar a análise* (ou
A) a para, e um ponto no botão *Tabuleiro* da barra indica quando ela está rodando. O engine dela não é um dos
processos do torneio: o fim de um torneio não o encerra. Parada a análise, o engine dela continua iniciado para a
próxima busca, com sua tabela hash (e a memória de GPU de um engine de rede neural): o botão de ligar/desligar ao lado
do engine, *Descarregar o engine* (também na paleta de comandos), o encerra, e também o da ameaça.

### Analisar uma partida inteira
<!-- en: analysing-a-whole-game a7e6166c -->

**Analisar a partida** (G, nas ferramentas da página ou no cabeçalho do gráfico) faz o engine de análise (as threads e
o hash dele, uma linha) analisar cada posição da partida em segundo plano, primeiro por 20 ms cada uma, depois de novo
por quatro vezes mais tempo a cada passada (80 ms, 0,3 s, 1,3 s, 5 s…), até o tempo escolhido com a seta ao lado do
botão (0,3 s, 1,3 s, 5 s, 20 s por posição, ou *até ser parado*): o gráfico chega em um ou dois segundos e fica mais
nítido a cada passada. As posições vão da última à primeira, o que deixa o hash do engine guardar o que vem depois de
uma posição (medido: cerca de 16 % mais perto de buscas longas do que da primeira à última, pelo mesmo tempo),
começando pela posição do tabuleiro. O cabeçalho do gráfico indica a passada, o tempo por posição e o progresso, e uma
tira fina sob a faixa dos tempos, o que cada posição recebeu; *Parar* (ou G) a interrompe.

Cada lance é julgado pelas chances de vitória que o jogador que o fez perdeu, a partir das pontuações da análise antes
e depois dele (a medida do lichess: 50 % em 0,00, cerca de 59 % em +1,00): 10 pontos perdidos é uma imprecisão (`?!`),
20 um erro (`?`), 30 um erro grave (`??`), marcados depois do lance, no gráfico e no tabuleiro; o lance que o engine
teria jogado nunca é marcado. Um lance é julgado na última passada completa; uma marca que a última passada mudou fica
vazada até que a próxima a confirme. N e Shift+N (ou os botões ao lado da navegação) vão ao próximo e ao anterior
lance marcado.

Cada passada completa é guardada com a partida (em `analyses/` no perfil): a mesma partida, aberta de novo de qualquer
lugar, a mostra imediatamente, e G continua a partir das passadas feitas. A análise roda numa thread própria com um
processo próprio do engine, as threads dela contadas no orçamento de núcleos; a análise do tabuleiro (A) a pausa, e
quando A analisa uma posição da partida por mais tempo do que a passada, o valor de A toma o lugar do valor dessa
posição.

**Várias análises**: *+ Engine* na legenda do gráfico faz outro engine da biblioteca analisar a partida também, pelo
mesmo tempo a cada passada. Cada análise tem a sua cor no gráfico e no tabuleiro (o melhor lance dela como uma seta
enquanto a análise do tabuleiro está desligada), a sua linha em *Este lance* e as suas precisões na revisão. Elas
avançam juntas, passada a passada, todas ao mesmo tempo quando os núcleos permitem, uma depois da outra caso
contrário. Uma análise julga os lances (a primeira; um clique direito numa outra na legenda a torna o juiz, ou a
remove); uma marca fica vazada quando as análises não julgam todas o lance da mesma forma. As chances de vitória
comparam os engines melhor do que os centipeões, que diferem de um engine para outro: elas usam o WDL de cada engine
quando ele o fornece.

### O gráfico da partida
<!-- en: the-game-graph 14e02bad -->

Sob os lances, o gráfico da partida, o mesmo de *Ao vivo* e de *Partidas*:

- **Avaliação**: a análise que julga os lances desenhada como uma área branca e preta (a vantagem das Brancas acima da
  linha, a das Pretas abaixo), cada engine que jogou como uma curva da sua cor nos lances dele, as outras análises
  como curvas tracejadas; os lances marcados sobre a curva; os valores além de ±6 desenhados ao longo da borda na cor
  de aviso. Sem análise, a área é a média das avaliações dos dois engines.
- Os lances de livro hachurados, as fases sob o eixo (a abertura; o meio-jogo a partir da primeira posição com no
  máximo 10 peças além dos reis e dos peões, ou menos de 4 na fileira de fundo de um dos lados; o final a partir de no
  máximo 6, como o lichess divide uma partida), a zona que as tablebases conhecem tingida, os números dos lances ao
  longo da parte de baixo.
- Sob o eixo, o tempo de cada lance: o das Brancas para cima, o das Pretas para baixo.

As abas dele: *Chances de vitória* (a partir do WDL quando os engines o fornecem, senão pela fórmula do lichess),
*V/E/D* (as parcelas que os engines informaram, as vitórias das Brancas embaixo), *Tempo* (com o tempo de decisão),
*Profundidade* (com a seldepth), *Nós*, *Velocidade*, *Acessos às tablebases*, *Concordância* (quanto as avaliações
dos dois jogadores se afastam, cada lance contra o anterior, e as das análises em cada posição) e *Concordância das
PVs* (quantos meios-lances da PV de um engine repetem o que a do outro previa); as abas para as quais a partida não
tem valores ficam de fora, e as que não cabem vão para o menu `⋯`. Passar o mouse indica o lance e o que cada engine
disse; um clique ou um arrasto vai até o lance. Um clique numa curva na legenda a oculta ou a mostra; a seta à direita
recolhe o gráfico.

### Este lance
<!-- en: this-move a4a908f8 -->

Sob os lances, sobre o lance mostrado: o que disse o engine que o jogou (a pontuação, a profundidade e a seldepth, o
tempo, os nós, a velocidade, os acessos às tablebases, como o PGN os tem), a linha que ele esperava depois do lance, o
veredito de cada análise (a parte das chances de vitória perdida, o julgamento, o melhor lance e a pontuação dele) e,
com as tablebases, o resultado depois do lance e se o lance manteve o de antes. *Mostrar* põe uma linha no tabuleiro
sem mexer na partida: a posição depois dela, o tabuleiro enquadrado e, por cima dele, o que ela é; Esc, um clique no
tabuleiro ou um lance percorrido na partida traz a partida de volta. No tabuleiro, o julgamento do lance é um selo na
casa dele, e a resposta que o engine dele esperava, uma seta tracejada.

### Variantes
<!-- en: variations 7932794c -->

Um lance jogado no tabuleiro onde a partida seguiu outro caminho começa uma variante: a partida fica como veio, com as
análises e o gráfico dela. Os lances mostram as variantes entre parênteses depois do lance que elas substituem,
aninhadas e numeradas; um clique num lance vai até lá, e *Voltar à partida* volta à posição de onde a variante sai. Um
clique direito num lance de uma variante a promove (ela vira a linha de onde parte, cujos lances a partir dali viram a
variante), a apaga junto com as que partem dela, ou a copia. *Copiar PGN* escreve as variantes, e as de um arquivo PGN
aberto são lidas (comentários, números, anotações e resultados ignorados; um lance que não pode ser jogado encerra só
a variante dele). O gráfico, as análises e os julgamentos são os da partida; numa variante, o gráfico marca onde ela
sai da partida.

### Os lances, a tabela e a revisão
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Lances** escreve a partida como texto corrido: figurinhas (veja [Aparência](#aparência)), os lances de livro em
cinza e o fim do livro marcado, os julgamentos em cor, o lance atual enquadrado; passar o mouse sobre um lance indica
o que o engine dele disse e o julgamento. **Tabela** lista cada lance com a pontuação do engine dele (do ponto de
vista das Brancas), a profundidade e o tempo, uma barra para o tempo, os lances marcados coloridos.

**Revisão**, depois que a partida foi analisada: para cada lado a precisão (a do lichess: a de cada lance segundo as
chances de vitória perdidas, os lances dos momentos tensos pesando mais), as imprecisões, os erros e os erros graves,
a perda média em centipeões, a parte dos lances que a análise também jogaria, a profundidade e o tempo médios do
engine; a precisão na abertura, no meio-jogo e no final; com várias análises, as precisões que cada uma dá. Depois os
momentos-chave, cada um a um clique: a virada (o lance que mais perdeu, com o melhor lance), os outros erros e erros
graves, onde os dois engines discordaram em 1,5 peão ou mais por dois lances seguidos, o começo do final e o das
tablebases. *Quem viu primeiro* indica, para a virada, em que lance cada engine que jogou e cada análise deu pela
primeira vez um peão ou mais ao lado que ganha. *Copiar o PGN com a análise* escreve a partida com a avaliação de cada
posição (`[%eval]`), os julgamentos como NAGs e, depois de um lance marcado, a linha que a análise preferia; *Copiar
como BBCode* dá a revisão para um fórum.

### Tablebases na página Tabuleiro
<!-- en: tablebases-on-the-board-page 721b71dd -->

Com as pastas Syzygy da configuração (*Adjudicação* > tablebases), a página Tabuleiro consulta cada posição da
partida: o gráfico tinge a zona que as tabelas conhecem, *Este lance* indica o resultado depois do lance e se o lance
manteve o de antes ou o estragou, e a revisão marca onde as tabelas assumem. Cada posição é consultada como se uma
captura ou um lance de peão tivesse acabado de ser jogado, o que é exato exceto quando a regra dos 50 lances está
perto. As tabelas são as do processo do programa: elas não são consultadas enquanto um torneio joga nele.

### Jogar uma partida a partir de uma posição
<!-- en: playing-a-game-from-a-position 7e83c318 -->

**Jogar uma partida…** joga uma partida a partir da posição do tabuleiro entre dois engines da biblioteca, ou entre você
e um deles (veja *Humano* mais abaixo) (*Brancas* e *Pretas*, `<->` os troca junto com os relógios), com um relógio para
cada um (minutos e um incremento em segundos; *O mesmo para os dois*, ou um para cada, para dar vantagem de tempo), e,
se marcado, uma segunda partida com as cores invertidas. Os engines são iniciados com os valores de opções da
biblioteca, um engine WinBoard pelo adaptador. O tabuleiro acompanha a partida enquanto ela é jogada (voltar nela para
de acompanhar até chegar de novo ao fim, ou até um clique em *Voltar ao lance atual*), com os relógios, a busca do lado
que tem a vez e *Parar*, que encerra a partida sem resultado. Uma partida termina por mate, afogamento, material
insuficiente, a regra dos 50 lances, uma tripla repetição, um lance ilegal, um engine que para, ou uma perda por tempo
(o relógio concede 0,1 s para o lance chegar). Cada partida é adicionada, quando termina, a um arquivo PGN em `games/`
no perfil (com o nome da data e dos engines, e uma tag `Termination`), que *Abrir as partidas* lista no tabuleiro.

As partidas são jogadas pelo próprio cliente de engines do fastchess-gui, à parte de qualquer torneio: elas rodam
enquanto um torneio é jogado, sem mexer nele. As threads delas (o maior valor entre os dois engines, já que eles jogam
alternadamente) contam no orçamento de núcleos; quando as partidas dos torneios em andamento e a análise já ocupam a
máquina, o cartão avisa e oferece *Jogar mesmo assim* em vez de *Jogar*. A análise para quando uma partida começa, já
que competiria com os engines dela.

**Humano**, primeiro nas listas de *Brancas* e *Pretas*, é você: você joga esse lado; seus lances são feitos no
tabuleiro com o mouse, no tabuleiro plano como no 3D (um peão que chega à última fileira pede a peça). O tabuleiro gira
para deixar o seu lado embaixo a cada partida (*Girar* continua girando-o), só aceita suas peças na sua vez (o cartão
mostra *Sua vez*), e um clique enquanto você olha um lance anterior o traz de volta à partida. Seu relógio corre como o
de um engine, perda por tempo incluída: dê a si mesmo mais tempo que ao engine desmarcando *O mesmo para os dois*.
Enquanto você joga, o que o engine pensa fica guardado (a avaliação, a profundidade e a linha dele, portanto também a
barra de avaliação e a curva do gráfico); a partida terminada tem tudo isso, pronta para *Analisar a partida*.
*Abandonar a partida* a encerra a qualquer momento, vencida pelo engine; com uma segunda partida, ela vem a seguir, com
a sua cor invertida. Suas partidas são salvas com o seu nome de usuário, e só o engine conta no orçamento de núcleos.
Não dá para voltar um lance, e o engine joga com força total, a menos que o cartão dele defina uma menor
(`UCI_LimitStrength` e `UCI_Elo`, ou `Skill Level`, quando ele os tem).

## Suítes de testes
<!-- en: test-suites 42c2b2ba -->

A página **Suítes** roda uma suíte de testes EPD: posições com o melhor lance (`bm`), os lances a evitar (`am`), ou,
para a Strategic Test Suite, os pontos de vários lances (`c0 "f5=10, Be5+=2"`).

- **Suíte**: um arquivo EPD, ou um livro EPD da biblioteca. As linhas sem uma posição legal ou sem uma dessas
  operações ficam de fora (o número delas é mostrado).
- **Engines**: qualquer número de engines da biblioteca, com as opções deles; *Hash* e *Threads* impõem um valor a
  todos (0 = o de cada engine).
- **Tempo**, **Nós** ou **Profundidade** por posição. *Encontrado há (meios-lances)*: uma busca para assim que uma
  solução continuou sendo o primeiro lance do engine por esse número de meios-lances de profundidade a mais (0 = toda
  busca usa o tempo inteiro).
- **Paralelismo automático**: com um tempo, núcleos físicos − 1 divididos pelas threads dos engines; com nós ou uma
  profundidade, todas as threads de hardware.

Uma posição é resolvida quando o lance final do engine é uma solução: um dos `bm`, nenhum dos `am`, ou, só com
pontos, o lance que vale mais. O tempo até a solução é o momento em que o engine se fixou nesse lance. A grade mostra
as posições contra os engines, ✓ com o tempo até a solução ou ✗ com o lance jogado, e os pontos STS; os totais vêm
primeiro: posições resolvidas, o tempo médio delas, e a pontuação STS. Uma execução concluída é guardada em `suites/`
no perfil; *Comparar com uma execução salva…* adiciona as colunas de uma execução anterior da mesma suíte, para
comparar versões de um engine. *Exportar CSV…* grava a grade.

## Novo torneio
<!-- en: new-tournament 6d169de4 -->

**Torneios → Novo torneio** abre a configuração: um cartão de linhas recolhidas que dizem como as partidas são
jogadas, os participantes com a biblioteca de engines ao lado, e um resumo ao vivo à direita (número de partidas,
partidas por engine, duração estimada, aberturas, configurações comuns, vantagem de tempo). A configuração do último
torneio é guardada, então testar de novo duas versões de um engine é: escolher A e B, **Iniciar**.

Tudo na página pode ser feito pelo teclado: Tab e as setas passam entre as linhas, as listas e os botões, Enter ou
Espaço abre uma linha ou uma lista, e as teclas dos [participantes e da
biblioteca](#participantes-e-a-biblioteca) funcionam ali. **Iniciar** cria o torneio e o joga; **Adicionar à fila**
o guarda para depois (veja [Fila e relançamento](#fila-e-relançamento)).

### O resumo e suas verificações
<!-- en: the-summary-and-its-checks 5609344a -->

O resumo dá a memória que as tabelas de hash podem ocupar: dois processos de engine por partida jogada ao mesmo
tempo, e os engines que aguardam a próxima partida, cada um com o seu `Hash`. Acima da memória livre ele avisa (o
sistema vai usar swap), nomeando o engine com o maior `Hash`; acima da memória instalada o torneio não começa.
`--run` diz o mesmo. Quando o orçamento de memória de **Preferências** deixa jogar ao mesmo tempo menos partidas do
que o paralelismo, o resumo diz quantas (cada partida ocupa o `Hash` dos seus dois engines e o que os processos deles
ocupam além disso), e a linha *Paralelismo* também; o mesmo para as partidas com um engine que usa uma GPU, em
relação às *GPUs*. Ele também nomeia os engines cujo programa mudou desde o último torneio deles (uma nova
compilação), com *O que mudou* quando a ficha do engine lista novos padrões ou opções (veja [Programas
recompilados](#programas-recompilados)).

O que daria errado é dito com o engine, o recurso e a correção, um botão sob a linha (a paleta de comandos lista as
mesmas correções):

- um programa ou uma pasta de trabalho não encontrados: *Abrir o engine* (a ficha dele na página Engines),
  *Localizar…*; um livro de aberturas não encontrado: *Localizar…*;
- um engine cujas próprias `Threads` (a configuração dele, senão o padrão: alguns engines ocupam todos os núcleos)
  passam da metade das threads de hardware, ou das threads que cada partida tem com as partidas jogadas ao mesmo
  tempo (os núcleos físicos, com relógio): *Baixar Threads para N* define a opção do engine na biblioteca, *Abrir o
  engine*. `Threads` dadas pela configuração além do que a máquina tem recebem *Baixar Threads* onde são dadas (a
  célula do participante, as configurações comuns). A lista de participantes marca um engine assim com um *!*, os
  engines do match são sinalizados abaixo deles;
- um valor de opção que um engine recusa (fora da faixa, que não é uma das escolhas, uma opção que ele não tem mais):
  o fastchess não o envia, e o engine joga com o padrão dele;
- as tablebases Syzygy que o torneio usa: as pastas da adjudicação e aquilo para onde aponta o `SyzygyPath` de cada
  engine (várias pastas separadas por `;` no Windows, `:` nos outros sistemas). Para as peças usadas (o *Máx. de
  peças* da adjudicação, senão as maiores tabelas encontradas), todo arquivo WDL (`.rtbw`) deve estar lá, e nenhum
  arquivo truncado (um arquivo Syzygy tem 16 bytes a mais que um múltiplo de 64; os engines deixam de lado um arquivo
  de outro tamanho, e alguns param quando o encontram): *Syzygy 6 peças: faltam 14 arquivos em D:\TB
  (KRPPvKR.rtbw…)*, *Adjudicação até 6 peças, mas só há arquivos de 5 peças*, *SyzygyPath de Alpha aponta para uma
  pasta sem arquivos de tablebase*, com *Abrir a pasta* e *Baixar para 5 peças*; a dica lista os arquivos. São
  avisos: a adjudicação e os engines se viram sem os arquivos ausentes. Só uma adjudicação sem pasta, ou cujas pastas
  não contêm nenhuma tabela, impede o torneio de começar (o fastchess a recusa): *Localizar…* escolhe outra pasta. As
  pastas são lidas em segundo plano, e de novo a cada poucos segundos enquanto o resumo é mostrado.

Um início recusado nomeia os mesmos problemas, com as correções e *Abrir o log*. Cada problema tem um **?** que abre
o item correspondente em [Solução de problemas](#solução-de-problemas).

### Perfis e modelos
<!-- en: profiles-and-templates df08e65d -->

**Perfis.** As cinco primeiras linhas, *Formato*, *Controle de tempo*, *Aberturas*, *Adjudicação* e *Configurações
dos engines*, são cada uma um perfil com nome: a lista dela escolhe um, a linha ao lado diz o que ele joga ("40
lances em 15 min, repetido", "empate 40/8/10 cp · TB 6 peças"; a dica dá o mesmo como o cutechess e o fastchess o
escrevem, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), e ✎, ou um clique nessa linha, abre os detalhes
ali mesmo, abaixo dela. Uma alteração feita só para este torneio põe um ponto ao lado do perfil, e a lista e os
detalhes dele oferecem *Atualizar* (o perfil recebe a alteração, para todos os torneios e modelos que o usam),
*Reverter* e *Salvar como perfil…* (com um novo nome). O programa vem com predefinições:

- controles de tempo: 10+0.1 e 60+0.6 (os testes curto e longo do fishtest), 2+1, 40/4, 40/15 e 40/20 (40 lances em
  4, 15 ou 20 minutos, repetidos, como as listas de rating os jogam), e 200k nós por lance;
- formatos: SPRT ganho [0, 3] e não regressão [-5, 0], um match de 1000 partidas, um todos contra todos, um gauntlet,
  um suíço de 9 rodadas, um mata-mata e um match por equipes;
- adjudicações: nenhuma, uma padrão (empate a partir do lance 40 depois de 8 lances dentro de 10 cp, derrota depois
  de 3 lances a 600 cp) e a mesma com as tablebases até 6 peças;
- configurações dos engines: as de cada engine, ou Hash de 16, 64 ou 256 MB com 1 thread e os livros próprios dos
  engines desligados;
- aberturas: sem livro, ou as 960 posições do Chess960.

Um perfil seu salvo com o nome de uma predefinição toma o lugar dela; excluí-lo (pela lista dele, após uma
confirmação) traz a predefinição de volta, e as configurações e os modelos que o usavam mantêm os valores dele. Os
perfis são guardados em `profiles.json` na pasta do perfil, e a paleta de comandos (Ctrl+K) os lista: escolher um o
aplica à configuração. A paleta também abre cada linha da configuração pelo título, e carrega ou salva modelos. As
pastas Syzygy não estão em nenhum perfil: elas pertencem a este computador, e a adjudicação e os engines as
compartilham. As linhas *Paralelismo* e *PGN e outros* contêm o que é só deste torneio.

**Modelo** e **Nome**, ao lado de *Iniciar*: um modelo é a configuração inteira, um perfil para cada seção mais o
que nenhum perfil contém (o nome, os participantes, o paralelismo, os arquivos PGN, a transmissão). O menu carrega um
ou salva a configuração como um (*Salvar a configuração como modelo…*, que pergunta antes de substituir um modelo com
o mesmo nome), e a paleta de comandos também os carrega. Uma seção que seguia o seu perfil quando o modelo foi salvo
segue esse perfil, então atualizar um perfil atualiza todos os modelos que o usam; uma seção alterada para aquele
torneio mantém os próprios valores. Os modelos salvos por versões anteriores carregam como eram, e as seções deles
recebem os perfis equivalentes ao que jogam. Um nome vazio é formado a partir dos engines.

### Formato
<!-- en: format 29a36e7c -->

A dica de cada formato diz o que ele é.

- *Match*: dois engines, A (o engine em teste; todas as estatísticas são do ponto de vista dele) e B (a referência).
  Um número fixo de partidas, até você parar, ou um SPRT: elo0, elo1, alpha, beta, modelo (`normalized` usa limites
  em nElo como o fishtest e o OpenBench, `logistic` o Elo clássico, `bayesian` o BayesElo) e um número máximo de
  partidas opcional. Os perfis SPRT ganho e não regressão dão os limites habituais, e a linha sob os limites diz
  quantas partidas o teste deve durar. *Calculadora…* desenha, como a calculadora de SPRT do fishtest, a
  probabilidade de H1 ser aceita e o número esperado de partidas em função da verdadeira diferença de Elo, a partir
  de uma taxa de empates esperada e do viés RMS das aberturas; *Usar estes limites* copia de volta para a
  configuração os limites que lhe foram dados. *Viés Elo* (0 por padrão) é somado aos dois limites, para testar
  contra uma versão sabidamente mais forte ou mais fraca nessa medida: [0, 3] com um viés de 5 dá o SPRT [5, 8], que
  o resumo, a visão geral e o modo headless escrevem `SPRT [0, 3] + 5`; a calculadora e a linha sob os limites o
  levam em conta. *Partidas mín.* (0, nenhum, por padrão) segura a decisão até que esse número de partidas seja
  jogado, com o LLR ainda calculado e mostrado, para proteger um teste curto de uma parada precoce por sorte: um LLR
  além de um limite antes disso é indicado sob a barra do LLR e o match continua; quando as partidas estiverem lá, o
  próximo par que terminar com o LLR além de um limite encerra o match. Um máximo de partidas abaixo do mínimo
  encerra o match sem decisão. Esta é a forma mais rápida de comparar duas versões de um engine, e nada mais é
  necessário.
  *Cores* (*Alternadas* por padrão) diz quem joga de Brancas: A e B alternadamente, ou *A sempre de Brancas* ou *B
  sempre de Brancas*, como `-noswap` (e `-reverse`) do fastchess, para partidas com handicap em que o engine mais forte
  deve ficar sempre com o lado em desvantagem, ou uma abertura testada por um só lado. As partidas continuam em pares,
  ambas com as mesmas cores, mas cada partida tem sua própria abertura, a seguinte do livro (como `-games 1` do
  fastchess): os pares e suas estatísticas permanecem, a pontuação inclui a vantagem das Brancas, e o viés RMS, que
  inverter as cores anula, não é mostrado. O resumo e o cabeçalho do match o indicam.
- *Todos contra todos*: cada engine joga contra todos os outros, *Ciclos* vezes, cada vez duas partidas com as cores
  invertidas (o que o cutechess chama de rodadas, de 2 partidas por encontro). As rodadas seguem as tabelas de
  Berger, então cada engine joga uma vez por rodada.
- *Gauntlet*: os primeiros engines da lista, tantos quanto *Engines testados* (os cabeças de chave), jogam contra
  todos os outros; os outros não jogam entre si.
- *Suíço*: um número de *Rodadas*. Cada rodada são duas partidas, com as cores invertidas, contra um engine com
  pontuação parecida; ninguém enfrenta o mesmo adversário duas vezes quando isso pode ser evitado. Com um número
  ímpar de engines, o engine de pior classificação que ainda não teve folga recebe uma, que vale um ponto.
- *Mata-mata*: os engines são cabeças de chave na ordem da lista, os primeiros cabeças de chave recebem as folgas
  quando o número de participantes não é uma potência de dois, e os dois primeiros cabeças de chave só podem se
  enfrentar na final. Um match tem *Pares de partidas por match* pares de partidas; um match empatado ganha mais um
  par de cada vez, até dez, depois do que o cabeça de chave mais bem colocado passa.
- *Match por equipes*: duas equipes de engines, para comparar uma família de engines, ou um conjunto de versões, com
  outra como um todo. *Emparceiramentos*: *Cada engine contra todos*, cada engine de uma equipe contra cada engine da
  outra equipe, um adversário por rodada (um sistema Scheveningen; as equipes podem ter tamanhos diferentes), ou
  *Tabuleiro a tabuleiro*, o primeiro engine de cada equipe contra o primeiro da outra, o segundo contra o segundo e
  assim por diante (equipes do mesmo tamanho). Cada emparceiramento é jogado *Ciclos* vezes, cada vez duas partidas
  com a mesma abertura, com as cores invertidas; as cores da primeira partida alternam de tabuleiro para tabuleiro e
  de um ciclo para o seguinte. *Equipe A* e *Equipe B* dão nome às equipes ("Equipe A" e "Equipe B" sem um nome); ao
  lado delas, quantos engines cada uma tem. As equipes são formadas na lista de participantes: a metade de cima da
  lista é a equipe A, a metade de baixo a equipe B (a equipe A fica com o que sobra), até que engines sejam colocados
  numa equipe.

### Participantes e a biblioteca
<!-- en: participants-and-the-library a7ef2e55 -->

**Participantes**, com a **Biblioteca** ao lado (abaixo, numa janela estreita). Cada participante é uma linha: a
posição (uma estrela para os cabeças de chave de um gauntlet), o nome, o Elo de referência, o que ele joga de
diferente das configurações comuns ("Hash 512 MB · 80 % do tempo") e ⋯, destacado quando ele tem configurações
próprias. ⋯, um clique direito ou a tecla Menu as abre: o `Hash` e as `Threads` próprios, nós ou profundidade
fixos, vantagem de tempo (uma porcentagem do controle de tempo, para um relógio), o próprio *Livro*, *Este engine
mais uma vez, com outro livro*, os comandos para movê-lo na lista e *Remover*. A ordem da lista é a ordem dos
cabeças de chave: arraste os participantes para reordená-los (cabeças de chave primeiro), ou pressione Ctrl+Cima ou
Ctrl+Baixo para mandá-los para o topo ou para o fim, Alt+Cima ou Alt+Baixo para movê-los uma posição. Ctrl ou Shift
clique seleciona vários; Delete (ou *Remover*) os remove.

Num match por equipes, a posição é a equipe e o tabuleiro, A1, A2… para a equipe A, B1, B2… para a equipe B, na
ordem da lista (tabuleiro a tabuleiro, A1 joga contra B1). O menu ⋯ coloca o participante numa equipe; *Equipe A* e
*Equipe B* abaixo da lista colocam numa delas os participantes selecionados, e *Metades* refaz as equipes a partir
das metades da lista (a paleta de comandos oferece o mesmo). Um engine adicionado depois que as equipes foram
escolhidas entra na menor. O resumo diz quantos engines cada equipe tem, e um início é recusado, com o motivo, quando
uma equipe está vazia ou, tabuleiro a tabuleiro, quando as equipes têm tamanhos diferentes.

A biblioteca lista os engines ativos com o Elo de referência, ordenados por nome ou por Elo (clique na coluna), um
`!` nos que têm um problema (programa ausente ou recompilado, última verificação com falha: a dica diz qual) e um ✓
nos que já jogam. O filtro pesquisa os nomes, os nomes que os engines informam, os autores, os programas, as notas e
os grupos (Ctrl+F lhe dá o teclado; Enter nele adiciona os engines selecionados, ou o primeiro mostrado); uma
etiqueta por grupo mostra só aquele grupo, e *Adicionar todos os mostrados* adiciona o grupo inteiro. Dê um duplo
clique num engine, ou selecione vários com Ctrl ou Shift (Ctrl+A: todos os mostrados) e pressione Enter, para
adicioná-los; ou arraste-os para um lugar entre os participantes. *Sugerir…* seleciona os N engines ativos que
funcionam e cujo Elo de referência é o mais próximo do do primeiro participante (o engine em teste de um gauntlet), a
no máximo 150 de distância por padrão; Enter os adiciona. Para um match, a biblioteca preenche A, depois B (B é
substituído quando os dois já foram escolhidos), e um engine arrastado sobre A ou B toma o lugar dele.

### Livros próprios dos participantes
<!-- en: participants-own-books 580e52cc -->

*Livro*: um livro de aberturas próprio do participante, como o `book=`/`bookdepth=` do Cute Chess: um livro Polyglot
(`.bin`) ou PGN da página Livros e até que profundidade ele é seguido, em meios-lances desde o início da partida, a
abertura do torneio incluída (*tudo*: enquanto o livro tiver um lance; para um livro PGN, os 40 primeiros
meios-lances das partidas dele). Um livro PGN é primeiro transformado em livro Polyglot: os lances de cada partida
até essa profundidade, cada lance com peso igual ao número de partidas que o jogam; as partidas com um lance ilegal
ficam de fora e são contadas. Isso roda em segundo plano assim que o livro é escolhido (progresso no botão *Livro*,
na janela dele e no resumo) e é guardado na pasta `book-cache` do perfil até que o arquivo ou a profundidade mude;
um início que vem antes do fim espera por isso. Um livro que não pode ser preparado mostra por quê na janela dele,
com *Tentar novamente*; cada início também tenta de novo. Um torneio joga com o livro preparado quando ele foi
iniciado ou retomado, mesmo que o arquivo PGN mude nesse meio-tempo.

Enquanto a posição estiver nesse livro, o participante joga o lance do livro imediatamente, escolhido pelos pesos do
livro conforme a *Aleatoriedade* das aberturas os ajusta (também na janela *Livro*, a mesma para todos os livros do
torneio: veja [Aberturas](#aberturas)); depois o engine dele assume. As aberturas do torneio, se houver, são jogadas
primeiro, por todos os engines; os livros EPD só fornecem essas e ficam acinzentados na lista. Para comparar livros
com um engine, escolha *Este engine mais uma vez, com outro livro* no menu ⋯ dele: cada cópia joga como
`<engine> (<book>)`, de modo que os resultados e o rating dela ficam separados. Num match os botões *Livro* ficam ao
lado de A e B, e um engine pode jogar contra si mesmo com dois livros. Os lances de livro são marcados com `{book}` no
PGN; passar o mouse sobre um nome na classificação mostra o livro dele. Os engines WinBoard também podem ter um.

### Configurações dos engines
<!-- en: engine-settings 7a93d075 -->

*Hash* e *Threads* impostos a todos os engines, também num match (0 = o de cada engine; o próprio de um
participante, do menu ⋯ dele, vem primeiro); *Tablebases Syzygy para os engines* dá as pastas de tablebases (as
mesmas da adjudicação) a todo engine com uma opção `SyzygyPath`, e *Livros próprios dos engines desligados* define
`OwnBook` como false onde ele existe, para que toda partida seja jogada a partir das aberturas do torneio. Os dois vêm
desligados por padrão.

### Controle de tempo
<!-- en: time-control 672cb041 -->

Tempo base e incremento em segundos, lances por período opcionais, e uma margem antes de uma partida ser perdida por
tempo; um tempo fixo por lance (`go movetime`, com a mesma margem); um número fixo de nós por lance; ou uma
profundidade fixa em meios-lances (`go depth`), com a qual nenhum engine pode perder por tempo.

### Paralelismo
<!-- en: concurrency c02ee2cf -->

O número de partidas jogadas ao mesmo tempo. No modo automático, ele é medido na inicialização:

- nós ou profundidade fixos: uma partida por thread de hardware, já que o Hyper-Threading não muda os lances jogados;
- com relógio: núcleos físicos − 1, para que os engines não atrasem uns aos outros.

A linha e o texto sob o campo dizem qual regra escolheu o número. *Afinidade de CPU* (desligada por padrão; ausente
no macOS, que não a tem) fixa cada partida num processador, compartilhado pelos seus dois engines, já que eles nunca
pensam ao mesmo tempo. O fastchess só a suporta para engines de uma thread, então ela é desligada automaticamente
quando um engine tem `Threads` > 1. Um aviso aparece quando paralelismo × threads passa das threads de hardware (ou,
com relógio, dos núcleos físicos). *Jogar em* escolhe a máquina em que o torneio é jogado (veja [Um torneio numa
máquina](#um-torneio-numa-máquina)).

### Aberturas
<!-- en: openings 44656411 -->

Um livro EPD, PGN ou Polyglot (`.bin`) (uma suíte de aberturas), ordem aleatória ou sequencial, o número de
meios-lances mantidos de um livro PGN, o índice inicial e a semente aleatória (0 = uma nova ordem para cada torneio).
Cada abertura é jogada duas vezes, com as cores invertidas (em um match cujas *Cores* estão fixadas, cada partida a
partir de sua própria abertura).

Um livro Polyglot é seguido a partir da posição inicial até uma profundidade (8 meios-lances, se não for definida),
deixando de fora os lances com peso menor que um décimo do melhor da sua posição; as posições alcançadas são gravadas
uma vez em `<book>-<N>plies.epd` na pasta do torneio e jogadas como um livro EPD. Em ordem aleatória, elas são
gravadas na ordem em que são sorteadas, cada uma uma vez: uma linha vem tanto mais cedo quanto mais provável ela for,
pelos pesos dos seus lances, de modo que um torneio que joga só parte do livro joga primeiro as linhas principais; a
semente sorteia de novo a mesma ordem. Em ordem sequencial, elas mantêm a ordem do livro. *Aleatoriedade* (−100 a
100, 0 por padrão) diz como os pesos contam, como o Polyglot e o Cute Chess escolhem um lance de livro: 0 os pesos
como estão, −100 só o melhor lance de cada posição (uma linha, a menos que vários dividam o melhor peso), 100 todos os
lances por igual (os lances raros voltam); entre os dois, −50 eleva os pesos ao quadrado e 50 tira a raiz quadrada
deles. Ela escolhe também os lances dos livros próprios dos participantes, e é mostrada para eles mesmo com um livro
EPD ou PGN; o resumo a indica, e a linha sob as cópias da classificação dá o livro com a profundidade e, para um livro
Polyglot, essa aleatoriedade.

Num todos contra todos, num gauntlet, num suíço ou num match por equipes, *A mesma abertura para todos os pares de uma
rodada* dá a todos os pares de partidas de uma rodada a mesma abertura (tabuleiro a tabuleiro, um ciclo é uma rodada:
todos os tabuleiros jogam a mesma abertura), a seguinte do livro a cada rodada, para que nenhum engine receba aberturas
mais fáceis que os outros da sua rodada (um engine adicionado depois recupera o atraso em rodadas próprias, cada uma
com sua abertura).

*Marcar as aberturas desequilibradas* (desligado por padrão) mostra, à medida que as partidas chegam, as aberturas
que decidem a partida por si sós: uma partida é marcada quando termina se os dois engines viram o mesmo lado à frente
por pelo menos a pontuação (200 cp por padrão) em cada um dos seus primeiros lances depois do livro (4 por padrão; os
lances do livro próprio de um participante contam como lances de livro). Ela lê as pontuações dos engines, então um
engine que não dá nenhuma não marca nada; os resultados não são alterados. A aba *Partidas* filtra essas partidas
(*Abertura desequilibrada*), a aba *Aberturas* marca as aberturas delas com um `!` e as conta, e a visão geral conta
as partidas. *Jogar o par de novo a partir de outra abertura* anula um par assim depois que as suas duas partidas são
jogadas (elas ficam nos arquivos PGN e no diário, como qualquer partida anulada) e o joga de novo a partir de uma
abertura do fim do livro (na ordem em que o livro é jogado), que o torneio jogaria por último, para que as aberturas
dos pares seguintes não mudem; no máximo três vezes para um par, depois do que o par conta, marcado. O console e a
visão geral dizem quantas aberturas foram substituídas.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, nas aberturas: sem livro, as 960 posições iniciais em ordem aleatória (gravadas uma vez em
`chess960.epd` na pasta dos torneios); com um livro, as posições dele. Todo engine precisa jogar Chess960: os engines
UCI com a opção `UCI_Chess960`, os engines WinBoard com a variante `fischerandom`. O resumo nomeia os que não jogam, e
um torneio assim não começa.

### Adjudicação
<!-- en: adjudication a18dca1e -->

Empate (a partir de um número de lance, por N lances consecutivos, abaixo de uma pontuação), abandono (N lances
consecutivos acima de uma pontuação, opcionalmente com os dois engines de acordo), um número máximo de lances, e
tablebases Syzygy.

### PGN e outros
<!-- en: pgn-and-misc 6762bab8 -->

- Nome do evento: a dica dele nomeia os dois arquivos PGN em que cada partida é gravada.
- *Também adicionar cada partida a*: um arquivo PGN seu ao qual todo torneio adiciona as suas partidas, como o
  `-pgnout` do cutechess (nunca sobrescrito, no estilo de comentários ChessBase / Lichess ou cutechess), na ordem do
  calendário: as duas partidas de uma abertura lado a lado, as rodadas em ordem, qualquer que seja a ordem em que
  terminam. Uma partida espera ali até que as anteriores sejam gravadas; uma retomada continua; quando o arquivo não
  pode ser gravado (bloqueado, movido), as partidas esperam e o torneio continua, com um aviso na visão geral. Um
  crash ou uma queda de energia enquanto partidas são adicionadas nunca as grava duas vezes: a retomada verifica o
  arquivo e completa uma gravação interrompida, e nunca remove nada dele.
- Campos extras nos comentários dos lances (nodes, seldepth, nps, pv, tbhits, e o tempo de decisão `dt=`: quando o
  engine se fixou no lance que jogou; `wdl=W/D/L` é sempre gravado para os engines que o informam), ligados por
  padrão, já que os gráficos da aba *Partidas* os leem (pv deixa o PGN visivelmente maior).
- *Reiniciar um engine que der crash*, e *Parar o torneio quando um engine não inicia* (desligado por padrão: veja
  [Um engine que não consegue iniciar](#um-engine-que-não-consegue-iniciar)).
- Manter o log do engine de cada partida (ele sempre é mantido para as partidas que terminam mal).
- *Verificar as PVs de mate*: a PV de uma avaliação de mate deve ter o comprimento certo e terminar em mate. Toda PV é
  verificada quanto a lances ilegais, lances depois do fim da partida e um melhor lance que não é o primeiro lance
  dela; os lances com uma PV errada são marcados na aba Partidas e gravados no log.

### Dicas para as configurações
<!-- en: tips-for-the-settings 62afdb4d -->

- **Comparar duas versões de um engine**: um SPRT, ganho [0, 3] para uma alteração feita para ganhar força, não
  regressão [-5, 0] para uma simplificação ou uma limpeza. Primeiro um controle de tempo curto (10+0.1), depois um
  longo (60+0.6) para o que passar. Use um livro de posições equilibradas, não a posição inicial: senão, cada partida
  seria uma de poucas aberturas.
- **Paralelismo**: deixe-o automático. Com relógio, mais partidas do que núcleos físicos menos um fazem os engines
  atrasarem uns aos outros, e os resultados se desviam; com nós ou uma profundidade por lance, todas as threads de
  hardware podem jogar.
- **Threads**: 1 por engine para testes; muitas partidas de uma thread cada dão mais resultados por hora do que
  poucas partidas de muitas threads. Dê a todos os engines as mesmas `Threads` e o mesmo `Hash` em *Configurações dos
  engines*.
- **Hash**: o suficiente para o controle de tempo (16 a 64 MB em 10+0.1, 256 MB em 40/15 e mais longos); o resumo
  avisa quando o hash das partidas não cabe mais na memória.
- **Adjudicação**: o perfil padrão economiza tempo em torneios longos sem mudar muito os resultados; deixe-a
  desligada para testar os finais de um engine, ou use as tablebases.
- **Aberturas**: a ordem aleatória com uma semente fixa permite jogar um teste de novo exatamente igual; *A mesma
  abertura para todos os pares de uma rodada* é mais justa num todos contra todos com poucos ciclos.
- **Vários torneios**: dê ao mais importante um peso maior em vez de pausar os outros (veja [Vários torneios ao mesmo
  tempo](#vários-torneios-ao-mesmo-tempo)).

## Um torneio em andamento
<!-- en: running-a-tournament 9bcba803 -->

Um torneio em andamento ou aberto tem um espaço de trabalho com um cabeçalho (nome, progresso, *Pausar*, *Parar*,
*Clonar*, *Pasta*, *Layout*) e painéis, mostrados por padrão como abas:

| Aba | O que mostra |
|---|---|
| Visão geral | Os resultados até agora e as estatísticas, os gráficos, o checklist do envio |
| Classificação | A classificação como tabela ou tabela cruzada, os adversários de um engine, as cópias para um fórum |
| Equipes | Um match por equipes: o placar das equipes e a tabela cruzada das duas equipes |
| Chave | Um mata-mata: a chave, rodada a rodada |
| Calendário | Todos os pares de partidas, e as alterações do torneio |
| Transmissão | Arquivos ao vivo, o servidor TLCS, a página web e o lichess |
| Ao vivo | As partidas em andamento |
| Partidas | As partidas terminadas, os filtros delas, a reprodução e a exportação |
| Aberturas | Os resultados por posição inicial |

### Visão geral
<!-- en: overview 6985cebb -->

**Um match**: Elo e nElo com o intervalo de confiança de 95 %, LOS, placar, contagens pentanomiais, limites do SPRT e
LLR. *Estatísticas* (recolhido) dá os números do fishtest a partir das partidas e dos pares: placar, Elo, nElo e
BayesElo com DrawElo, desvio padrão por partida, o LLR de ambos, a razão de variância dos pares em relação às partidas
isoladas, o viés RMS das aberturas, os tipos de pares equilibrados e os pares ganhos sobre os perdidos, que *Copiar*
inclui. Depois os gráficos: Elo e LLR ao longo do tempo, duração das partidas, como as partidas terminaram e *Busca por
engine*.

**Um torneio**: a classificação (com *TF*, as perdas por tempo de cada engine, em laranja assim que houver uma; um *!*
ao lado de um engine que deu crash ou jogou um lance ilegal), *Busca por engine* logo abaixo, o progresso por ciclo ou
rodada, partidas por hora e tempo restante, os últimos resultados.

*Busca por engine* dá a média de cada engine nos lances que ele buscou, sem os lances de livro: profundidade, NPS, tempo
por lance, tempo de decisão e uso do processador; um clique numa coluna ordena os engines por ela. Ao lado do NPS, ×4
indica que o engine busca com 4 threads: ele conta mais nós do que com uma sem ser mais rápido por isso, então compare o
NPS dele com o de engines que tenham as mesmas. O NPS precisa de engines que o informem, e de *nps* em *PGN e outros*
para ser relido ao retomar.

Os dois contam as partidas de aberturas desequilibradas, e as aberturas substituídas, quando a configuração as
procura, com *Mostrar* para essas partidas. Um torneio concluído acrescenta um *Checklist do envio*: todas as
partidas jogadas, cada par com as duas cores, nenhuma partida duplicada, nenhum crash, perda por tempo ou lance
ilegal (com botões que abrem essas partidas), o hash de cada engine (mostrado, não julgado), ponder desligado, o livro
de aberturas, as tablebases.

### Classificação
<!-- en: standings e4198fcd -->

Pontos, partidas, perdas por tempo (*TF*), vitórias/empates/derrotas, ratings com o intervalo de confiança,
Sonneborn-Berger (Buchholz num suíço), folgas. *Tabela* os lista com um engine por linha; *Tabela cruzada* (todos
contra todos, até 200 engines) mostra cada emparceiramento. *Ordem:* por pontos, pontuação % ou ordem inicial (que
mantém no lugar as colunas da tabela cruzada de um gauntlet). Um clique num engine lista abaixo os adversários dele:
partidas, com Brancas, com Pretas, + = −, pontos e %, com os pares de que só uma partida foi jogada marcados; um
gauntlet abre com o engine testado selecionado.

*Copiar* como texto, ou como BBCode para um post num fórum (com uma linha que dá o formato, o controle de tempo, as
partidas e o livro), e num gauntlet *Tabela do gauntlet em texto* ou *Em BBCode, para um fórum* (o engine testado,
depois os adversários pelos próprios pontos deles, cada um com pontos, %, Elo ±, W-L-D); *Exportar HTML…*. Todos têm
*TF* como a classificação do modo headless, seguem a ordem escolhida, e terminam com uma linha que dá as partidas
jogadas das previstas, a taxa de empates, o controle de tempo, o livro (com os meios-lances mantidos de um livro PGN;
para um livro Polyglot, a profundidade e como os lances foram escolhidos, por exemplo `book:Perfect2023.bin (8
plies, moves by weight)`) e o evento. As cópias então assinam com *fastchess-gui* e a versão, e a tabela do gauntlet
é traçada com = em vez de -, para que uma tabela postada num fórum se distinga das de outros programas.

### Equipes
<!-- en: teams 8c845645 -->

Só num match por equipes, na aba *Classificação* (onde ele abre) e no topo da *Visão geral*: o placar das equipes,
"Lynx 17½ – 18½ Otter", com as partidas jogadas, a pontuação % da equipe A, a diferença de Elo que essa pontuação
representa com o intervalo de 95 % e o LOS, contados sobre os pares de partidas como as estatísticas de um match
(pentanomial); depois os engines de cada equipe com as partidas, os pontos, %, o rating e os pontos que cada um fez
contra cada engine da outra equipe (B1, B2… como colunas, o nome sob o cursor): a tabela cruzada das duas equipes.
*Tabela* e *Tabela cruzada* mostram os engines como em qualquer torneio. *Copiar* e *Exportar HTML…* dão os placares
das equipes e os engines das duas equipes uns contra os outros, com a linha abaixo deles.

### Chave e calendário
<!-- en: bracket-and-schedule fe207ac0 -->

*Chave*, só num mata-mata: a chave, rodada a rodada, com o placar de cada match. *Calendário*: todos os pares de
partidas por ciclo ou rodada, com o estado e a abertura que jogam (depois de iniciados, ou depois que a rodada deles
começou, quando a rodada compartilha uma), e as alterações do torneio (veja [Alterar um torneio em
andamento](#alterar-um-torneio-em-andamento)).

### Ao vivo
<!-- en: live 5561ed9b -->

As partidas em andamento, como um mosaico de tabuleiros ou uma partida em foco com os relógios, a busca em curso de
cada engine (profundidade, pontuação, nós, nps, PV em SAN), a lista de lances e o gráfico da partida (veja [O gráfico
da partida](#o-gráfico-da-partida)): as avaliações dos dois engines e, nas abas dele, as chances de vitória deles, o
tempo por lance com o tempo de decisão, a profundidade com a seldepth, os nós, a velocidade, os acessos às
tablebases, quanto eles se afastam, ou quantos meios-lances uma PV repete do que a PV do outro engine previa. As
parcelas de vitória/empate/derrota (`UCI_ShowWDL`, ligado para os engines que o têm quando são adicionados)
acompanham cada linha de busca; a aba *V/E/D* as desenha como faixas, as vitórias das Brancas embaixo.

Quando há espaço (uma partida em foco, um painel grande), cada engine tem seu cartão: o logo tão alto quanto o nome e o
relógio, o que ele faz (pensa, ou o lance que jogou), a busca número a número (profundidade e seldepth, avaliação, nós,
velocidade, os acessos às tablebases e o preenchimento do hash quando o engine os envia, o tempo gasto no lance até
agora) e toda a PV; o engine que espera mostra o que dizia ao jogar, com o momento em que se decidiu. Ao lado do
mosaico, o painel mantém uma linha de números e os 12 primeiros meios-lances da PV. *Avaliação do lado a jogar* |
*Avaliação das brancas*, à direita da aba, escolhe o lado de todas as avaliações dela: o do lado a jogar, como o
fastchess as escreve no PGN, ou o das brancas; a barra de avaliação e a tela grande mostram sempre o das brancas.

Acima dos lances: a abertura, o código ECO e o nome da posição mais avançada da partida que a tabela de aberturas
nomeadas conhece (lances do livro e dos engines igualmente, de modo que uma transposição depois do livro também recebe
nome), com o último lance do livro; e, assim que as tablebases Syzygy do torneio (as da adjudicação dele, senão as dadas
aos engines) conhecem a posição mostrada, o veredito delas: o lado que vence com a DTZ (os meios-lances até a próxima
captura ou lance de peão: as tablebases Syzygy não têm distância até o mate), ou o empate, com a regra dos 50 lances
contada pelo contador da partida, a menos que a adjudicação a ignore. Cada posição é consultada uma vez, fora das
threads que jogam as partidas. Na lista, cada lance de um engine tem sua avaliação, profundidade e tempo; ao passar o
mouse, aparecem o momento em que o engine se decidiu e a resposta que ele esperava (o segundo lance da PV: o fastchess
joga sem ponder). Os lances do livro dizem *livro*, uma linha marca o fim do livro, e o lance em que se pensa diz
*pensando*.

O mosaico tem um tabuleiro por partida em andamento, não por partida que o paralelismo permite: perto do fim de um
torneio, ou com menos partidas que isso, os tabuleiros restantes crescem. Um tabuleiro cuja partida terminou fica 10
segundos (a próxima partida da sua vaga começa ali), o da partida acompanhada fica, e quando nada está sendo jogado, as
últimas partidas ficam. Quando os tabuleiros precisariam de uma barra de rolagem, o mosaico tira largura da partida em
foco, até deixar 40 % da página para ela.

Sob o tabuleiro: o contador dos 50 lances (meios-lances desde a última captura ou lance de peão, de 100), a diferença
de material com as peças que um lado tem a mais, e as peças restantes, marcadas com *TB* quando a adjudicação por
tablebases pode encerrar a partida; *Detalhes sob os tabuleiros* os acrescenta ao mosaico. Abaixo deles, os botões
para percorrer os lances (veja [Os tabuleiros e seus botões](#os-tabuleiros-e-seus-botões)): a partida em foco, ou um
tabuleiro destacado, pode voltar lances enquanto a partida continua (também com ← → Home End, um clique num lance da
lista ou na curva), a posição anterior mostrada com o lance marcado na lista e na curva; *Voltar ao lance atual* (ou
End, ou um clique no último lance) faz o tabuleiro voltar a acompanhar a partida, como acontece a cada nova partida.

*Setas* (ligado por padrão) desenha o lance em que o lado que tem a vez está pensando, na cor do engine dele, e a
resposta que ele espera, mais clara, na cor do adversário, no tabuleiro em foco e nos destacados (não no mosaico),
quando ele mostra o lance atual. Num gauntlet, *Engine testado embaixo* (ligado por padrão) mostra cada nova partida
do lado do engine testado; girar o tabuleiro (F, ou o botão dele) continua a virá-lo até a próxima partida.
*Destacar* abre um tabuleiro numa janela própria. Um clique direito num tabuleiro encerra a partida dele com um
resultado (veja [Alterar um torneio em andamento](#alterar-um-torneio-em-andamento)). *Tela grande* mostra a partida
acompanhada ocupando a janela (veja [Tela grande](#tela-grande)).

### Tela grande
<!-- en: big-screen 14c8f0d6 -->

Para uma TV, um clube ou uma transmissão ao vivo, *Tela grande* (na aba *Ao vivo*, num tabuleiro destacado, ou na
paleta de comandos) mostra uma partida ocupando a janela, em claro sobre preto: os nomes dos jogadores e os relógios
em letras grandes (o lado que tem a vez e o relógio dele correndo em amarelo, um relógio abaixo de dez segundos em
vermelho), a barra de avaliação, a avaliação, a profundidade e a velocidade da busca, o último lance enquadrado no
tabuleiro e escrito em letras grandes, a abertura, as linhas dos engines (*Linhas*, ligado por padrão: a do lado a
jogar enquanto busca, em amarelo, a do outro desde o seu último lance, no máximo duas linhas cada), o veredito das
tablebases assim que conhecem a posição, e os lances em letras grandes, o último marcado; o resultado sobre o tabuleiro
quando a partida termina. O texto acompanha o tamanho da janela e tem um tamanho próprio (*A-* / *A+*, ou as teclas +
e -). Ela mostra a partida acompanhada, ou alterna entre as partidas em andamento a cada N segundos (*Cada partida por
vez*), ou acompanha *A mais interessante*: a mais próxima do equilíbrio, a mais longa entre as igualmente próximas,
mantida enquanto continuar assim. Os controles aparecem por três segundos quando o mouse se move (o ponteiro se
esconde com eles); ← e → trocam a partida, F11 coloca a janela em tela cheia, Esc sai. Um tabuleiro destacado movido
para outra tela (a TV) a mostra na própria janela, com F11 colocando essa janela em tela cheia enquanto a janela
principal continua; encaixado na janela principal, é a janela principal que a mostra.

### Partidas
<!-- en: games ba4a9604 -->

As partidas terminadas com a abertura (código ECO e nome), e um tabuleiro de reprodução: os botões sob ele, ← → Home
End e F, ou um clique num lance ou no gráfico; os mesmos detalhes sob ele e o mesmo gráfico da partida de *Ao vivo*
(veja [O gráfico da partida](#o-gráfico-da-partida)), a partir dos campos com que o PGN foi gravado: um gráfico cujo
campo o torneio não gravou fica no menu `⋯` do gráfico, em cinza, e avisa, com um botão para as opções PGN da
configuração.

**Filtros**: por resultado (vitórias, empates, derrotas, perdas por tempo, crashes e lances ilegais, adjudicações,
PVs erradas, e as partidas de uma abertura marcada como desequilibrada quando a configuração as procura); pelo que as
avaliações dizem (os engines discordam em N peões por 3 lances seguidos de cada um, a avaliação de um engine oscila N
peões de um lance dele para o seguinte, o perdedor achava que estava N peões melhor; a partida abre nesse momento); e
por abertura (um código ECO ou o começo dele, uma faixa como `B20-B99`, ou palavras do nome).

**Exportação** de uma, de todas ou das partidas filtradas: *Exportar tudo* propõe `<name>.pgn` ao lado da pasta do
torneio; os próprios arquivos PGN do torneio são recusados como destino. *Exportar para uma lista de rating…* grava as
partidas como os testadores de listas de rating (por exemplo, a CCRL) as enviam:
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, também um zip se pedido; os jogadores nomeados como
as listas os escrevem, `<engine> 64-bit 4CPU` (editável para cada participante), as rodadas renumeradas, as tags
`Event` e `Site` definidas, as partidas anuladas e as duplicadas deixadas de fora, e também as perdidas por crash ou
travamento, a menos que desmarcado; o `PlyCount` de uma partida perdida por lance ilegal conta só os lances gravados.
O testador e o local são lembrados.

*Log do engine* abre o log de uma partida que tem um (veja [Arquivos](#arquivos)). Um clique direito numa partida a
joga de novo, a anula ou define o resultado dela (veja [Alterar um torneio em andamento](#alterar-um-torneio-em-andamento)).

### A aba Aberturas
<!-- en: the-openings-tab c51dd29a -->

Resultados por posição inicial: os pares de partidas dela, a pontuação das Brancas, os empates, e quantos pares as
Brancas ganharam duas vezes, as Pretas ganharam duas vezes (a abertura favorece uma cor) ou um engine ganhou duas
vezes (ela distingue os engines), com a duração média. Ordenados por qualquer coluna, filtrados para as
desequilibradas, as empatadas ou as decisivas, ou para as *Marcadas como desequilibradas durante as partidas* (veja
[Aberturas](#aberturas) na configuração: marcadas com `!`, o número delas ao lado da contagem, com as aberturas
substituídas). *Exportar como EPD…* grava as posições mostradas, para guardá-las ou limpar um livro, com *Sem as
desequilibradas* (ligado por padrão, mostrado assim que uma abertura é marcada) deixando de fora as marcadas. Um
clique mostra a posição e as partidas dela. Em um match cujas *Cores* estão fixadas, cada partida tem sua própria
abertura: contam-se as partidas, e as colunas e filtros sobre os pares não são mostrados.

### Painéis e layouts
<!-- en: panels-and-layouts 9ed9b915 -->

Cada aba é um painel. Arraste um pela aba para encaixá-lo num lado de outro painel ou como aba ao lado dele, ou
solte-o em outro lugar para deixá-lo flutuando (também numa segunda tela no Windows e no Linux/X11, não no Wayland
nem com renderização por software). A partida acompanhada em *Ao vivo* se divide nos painéis *Tabuleiro*,
*Jogadores*, *Buscas*, *Lances* e *Gráfico*; as partes que não têm painel aberto ficam ao lado do mosaico em *Ao
vivo*, e os tabuleiros destacados se encaixam como os outros painéis. Feche um painel com o x da aba dele; traga-o de
volta por *Layout* > *Painéis*, ou com *Mostrar: …* na paleta de comandos, e ele volta encaixado.

*Layout* também oferece layouts prontos (*Abas*, como por padrão; *Acompanhar uma partida*: o tabuleiro à esquerda,
os jogadores, as buscas e o gráfico à direita, a classificação abaixo deles; *Torneio grande*: o mosaico ao lado da
classificação; *Transmissão*: um tabuleiro grande com os jogadores, as buscas e a classificação), *Salvar o layout
como…* (o seu, com um nome, guardado em `layouts.json` no perfil), *Excluir* e *Layout padrão*. Um painel tirado do
espaço de trabalho continua na tela nas outras páginas; a disposição é guardada entre as sessões.

### Uso do processador
<!-- en: processor-use e5234cbe -->

Enquanto as partidas são jogadas, o tempo de processador dos processos de cada engine (incluindo o engine por trás de
um adaptador WinBoard) é medido a cada `go` e a cada lance. A Visão geral mostra, para cada engine, os núcleos que ele
mantém ocupados enquanto pensa e a fração de um núcleo que usa enquanto o adversário pensa, e avisa quando um engine
usa claramente mais núcleos do que as suas `Threads`, ou pensa no tempo do adversário (ponder que não foi pedido, uma
thread em segundo plano): o que torna uma partida injusta para o adversário e para as outras partidas em andamento.
Ela precisa de 20 s de cada para concluir, e conta todas as partidas jogadas, na janela ou em modo headless (`--run`):
o que os engines de cada partida usaram é gravado em `processor.tsv` na pasta do torneio, então uma retomada o
mantém. Ela só avisa; nunca decide uma partida.

### Pausar, parar e retomar
<!-- en: pause-stop-and-resume 100c55cf -->

**Pausar** não começa nenhum novo par de partidas. As partidas em andamento vão até o fim, assim como a segunda
partida de um par cuja primeira partida já começou, então uma pausa nunca joga um resultado fora. **Retomar**
continua. **Parar** ou termina primeiro as partidas em andamento, ou para agora (as partidas interrompidas não são
contadas e serão jogadas de novo).

Um torneio parado por um engine diz qual no cabeçalho e na página inicial, em *Requer atenção*: *Interrompido: Alpha
não conseguiu iniciar: …* (depois da última tentativa), ou *Alpha travou ou parou de responder (partida 12)* (sem
*Reiniciar um engine que der crash*), com *Abrir o engine* e *Abrir o log*.

**Todo torneio é retomado exatamente de onde parou**, depois de uma parada, de uma janela fechada, de um crash ou de
uma queda de energia: nenhuma partida é perdida e nenhuma é jogada duas vezes, e um par de partidas jogado pela metade
é completado com a mesma abertura e as mesmas cores. *Retomar* na página inicial o continua, com as estatísticas das
partidas já jogadas: resultados, e a profundidade, o tempo por lance e o NPS dos engines relidos dos comentários dos
lances (o NPS só quando o PGN o registra, *nps* em *PGN e outros*). Quando o programa foi encerrado enquanto um
torneio estava em andamento (um crash, uma queda de energia, uma reinicialização, ou saindo durante o torneio), a
próxima inicialização o retoma sozinha, e a fila continua depois dele: *Retomar ao iniciar o torneio que estava em
andamento*, em **Preferências**, ligado por padrão. Um torneio em pausa ou parado espera por você, e `--autostart`
inicia em vez disso a configuração salva. Um participante cujo programa foi recompilado ou sumiu desde que ele entrou
é nomeado no cabeçalho do torneio retomado, com *Continuar* e *Pausar*: as próximas partidas dele são jogadas pelo
programa como ele é agora, com as opções que o torneio lhe deu.

### Vários torneios ao mesmo tempo
<!-- en: several-tournaments-at-once df7ce566 -->

Vários torneios podem ser jogados lado a lado: um gauntlet ao lado de um SPRT, dois gauntlets ao mesmo tempo.
*Iniciar* na página *Novo torneio* inicia outro enquanto alguns são jogados; *Retomar* retoma um ao lado deles. Cada
torneio é jogado num processo próprio (`fastchess-gui --run <folder>` comandado pela janela), então um crash derruba
só aquele torneio, e a janela, as verificações de engines, os testes rápidos e as suítes de testes não esperam mais
um torneio terminar.

O espaço de trabalho tem **uma aba por torneio aberto**, acima do cabeçalho: um ponto enquanto ele é jogado (um sinal
de pausa enquanto está pausado), o estado dele sob o cursor, um x para fechar um que não está sendo jogado. Os painéis
mostram a aba escolhida; a paleta de comandos lista as outras (*Mostrar: …*, *Pausar*, *Retomar*). Um torneio
concluído aberto pela lista toma a aba de um que estava só sendo consultado. A página inicial tem um cartão para cada
torneio em jogo, com o progresso, o líder, *Abrir* e *Pausar*, e abaixo deles o **peso**, os núcleos que as partidas
dele usam dos que ele pode usar, e o que a próxima partida dele espera; a barra *Núcleos* no topo mostra a parte de
cada torneio (e a da página Tabuleiro), e quantos núcleos os torneios dividem. O *Log* mostra as linhas de um torneio,
da janela, ou de todos (cada linha de um torneio leva o nome dele); as linhas de um torneio também são guardadas em
`fastchess-gui.log` na pasta dele.

**Dividir a máquina**: os torneios em jogo dividem um orçamento de núcleos, definido em **Preferências** (*Núcleos
para os torneios*: os núcleos físicos menos um por padrão). Cada um recebe primeiro o equivalente a uma partida
enquanto o orçamento permite, o de maior peso primeiro; o resto é dividido por peso (um torneio de peso 2 recebe o
dobro dos núcleos de um de peso 1), nenhum acima do próprio paralelismo, e o que um não pode usar vai para os outros.
Uma partida conta como as `Threads` do maior dos seus dois engines: um pensa enquanto o outro espera. Um torneio
pausado mantém só o que as partidas em andamento e as segundas partidas dos pares delas precisam. Uma parcela menor
nunca interrompe uma partida: o torneio joga menos partidas a partir da próxima. Um torneio sozinho joga tantas
partidas quanto o seu paralelismo indica.

**Memória**: uma partida só começa quando o `Hash` dos seus dois engines, e o que cada processo de engine ocupa além
disso (64 MB, e 8 MB por thread), cabem no que as partidas em jogo deixam do orçamento de memória (*Memória para os
motores* em **Preferências**, 75 % da memória instalada por padrão), dividido entre os torneios. O resumo da
configuração diz quantas partidas o orçamento deixa jogar ao mesmo tempo, e o cartão *Saúde* da página inicial avisa
quando há partidas esperando memória.

**GPUs**: um engine marcado com *Usa uma GPU* na ficha (um engine de rede neural como o Lc0) conta nas *GPUs* de
**Preferências**: não jogam ao mesmo tempo mais engines desses do que há GPUs, num torneio e entre torneios, enquanto
as outras partidas são jogadas ao lado deles. O resumo da configuração diz quantas partidas com um engine de GPU isso
permite, e avisa quando nenhuma GPU é contada, ou quando uma partida entre dois engines de GPU precisa de mais GPUs do
que a máquina tem (ela é então jogada sozinha).

**Deixá-los rodando**: fechar a janela enquanto torneios são jogados pergunta o que fazer. *Deixá-los rodando e sair*
(o padrão) deixa cada um sendo jogado no seu processo; a janela os mostra de novo, no ponto em que estão, na próxima
inicialização. *Parar e sair* os para, cada um para ser retomado exatamente de onde parou. O teste rápido para de
qualquer jeito. Um torneio cujo processo não existe mais na próxima inicialização (uma reinicialização, um crash) é
retomado como qualquer torneio interrompido. A transmissão da aba *Transmissão* roda na janela, para o torneio
mostrado: para uma transmissão que continue sem a janela, inicie o torneio a partir de um terminal com as opções de
transmissão (veja [Modo headless](#modo-headless)).

### Um engine que não consegue iniciar
<!-- en: an-engine-that-cannot-start 765a7c61 -->

Uma noite sem supervisão não para por causa de um engine. Um engine que não inicia (um programa movido ou
recompilado, um arquivo de rede faltando, um disco que sumiu) é tentado de novo três vezes, com 10 s de intervalo;
depois as partidas dele são postas de lado e as outras partidas continuam. Ele é tentado de novo a cada 5 minutos,
uma partida de cada vez, e as partidas dele voltam assim que ele inicia. O cabeçalho do torneio e a página inicial (em
*Requer atenção*) dizem qual engine, o erro, e oferecem:

- **Tentar novamente**: imediatamente, em vez de na próxima tentativa (depois de corrigir o programa, por exemplo).
- **Retirar…** e **Substituir…** (todos contra todos, gauntlets e matches por equipes): o painel de alterações da
  aba Calendário, preparado para esse engine, com o impacto mostrado antes de qualquer mudança.
- **Log do engine**: `logs/start-<engine>.log` na pasta do torneio, o que foi enviado aos engines na última tentativa
  e o que eles responderam, incluindo a saída de erro deles (muitas vezes o motivo: uma biblioteca ou um arquivo de
  rede não encontrado).

O log e a notificação da área de trabalho avisam quando um engine é posto de lado. O torneio só para quando não resta
nada para jogar sem os engines postos de lado, depois de uma última tentativa de cada um; o estado dele e a
notificação no fim avisam (*Parado: não resta nenhuma partida para jogar sem …*), e a fila continua com o próximo
torneio. *Retomar* tenta de novo todos os engines. Uma pausa suspende as tentativas. *Parar o torneio quando um engine
não inicia*, em *PGN e outros* da configuração, o para imediatamente em vez disso, depois que as tentativas se
esgotam.

### Alterar um torneio em andamento
<!-- en: changing-a-running-tournament aebe63e2 -->

O painel **Alterar o torneio** da aba *Calendário* funciona sem parar nada: as partidas em andamento continuam e
nenhuma partida jogada é perdida. Cada alteração mostra o impacto antes de ser aplicada.

- **Adicionar um engine**: ele joga os ciclos já começados (*Recuperar os ciclos já começados*) ou entra a partir do
  próximo ciclo.
- **Retirar um engine**: as partidas restantes dele são descartadas; as partidas jogadas até agora contam ou não.
- **Substituir um engine**: as partidas dele deixam de contar e são jogadas de novo pelo novo engine.
- **Número de ciclos**: mais ou menos.
- **Jogar partidas de novo**: todas as partidas de um engine, ou as perdas por tempo e os crashes.
- Clique direito numa partida (aqui, ou na aba *Partidas*): jogá-la de novo, anulá-la (sem repetição), ou definir o
  resultado dela.

Um engine adicionado ou que entra como substituto joga com as configurações do torneio (seu controle de tempo, Hash e
Threads, mostrados abaixo do engine), ou com *Configurações próprias*: Hash, Threads, parte do tempo (ou nós,
profundidade), como as configurações próprias de um participante na página *Novo torneio*. Um torneio criado antes da
0.6.6 não registrava suas configurações: as do seu primeiro engine fazem as vezes delas, e podiam ser as próprias desse
engine.

Num match por equipes, um engine adicionado entra na equipe escolhida e joga contra todos os engines da outra equipe;
tabuleiro a tabuleiro, não é possível adicionar um engine (o tabuleiro dele não teria adversário): substitua um em
vez disso. Um substituto fica com a equipe e o tabuleiro do engine que ele substitui; mais ciclos emparceiram os
engines que ainda jogam (tabuleiro a tabuleiro, nos tabuleiros do primeiro ciclo). As partidas que um engine retirado
ou substituído jogou ainda contam para a equipe dele, a menos que sejam anuladas.

**Definir o resultado** decide uma partida que o programa não soube decidir bem: um travamento que ele não detectou,
um final ganho em que os engines ficam andando em círculos, uma partida que terminou por um motivo errado. Escolha
1-0, ½-½ ou 0-1 e dê um motivo; os pontos que isso move são mostrados antes de *Definir o resultado* aplicá-lo. A
classificação, a tabela cruzada, os ratings e o checklist do envio contam o novo resultado (uma adjudicação: uma
perda por tempo transformada em empate não conta mais como perda por tempo); a aba *Partidas* a marca com um `*`, com
a dica dando o motivo e como a partida terminou quando foi jogada, e *Voltar ao resultado jogado* desfaz isso. Uma
partida em andamento é encerrada do mesmo jeito: clique direito no tabuleiro dela na aba *Ao vivo*, *Encerrar com um
resultado* (também na paleta de comandos para a partida acompanhada). O engine que tem a vez é parado, o fastchess
encerra a partida imediatamente e ela conta com o resultado escolhido; o engine inicia de novo para a próxima
partida. Isso precisa de *Reiniciar um engine que der crash* (configuração, *PGN e outros*, ligado por padrão).

As partidas anuladas e os resultados definidos à mão ficam nos arquivos PGN e no diário como foram jogados: os
arquivos PGN só recebem acréscimos. O que lê as partidas os aplica: as partidas anuladas ficam de fora, e um resultado
definido à mão está em toda exportação (*Exportar partida…*, *Exportar tudo…*, *Exportar para uma lista de rating…*,
a exportação do confronto direto da página *Ratings*, o arquivo de *Também adicionar cada partida a*) com
`[Termination "adjudicated by the operator"]` e o motivo num comentário antes do resultado. **Desfazer a última
alteração** é oferecido até a próxima partida começar: nada foi jogado ainda sob a alteração, então ela pode ser
desfeita exatamente (as partidas que já estavam em andamento quando ela foi feita são mantidas). Um resultado definido
à mão pode ser desfeito enquanto for a última alteração, tenham sido jogadas partidas depois ou não (num mata-mata,
cujas rodadas seguintes decorrem dos resultados, até a próxima partida começar); a aba *Partidas* também oferece
*Desfazer* ao lado da partida.

### Fila e relançamento
<!-- en: queue-and-relaunch 382fd33e -->

A **Fila** da página inicial guarda torneios a jogar um depois do outro: quando um termina sozinho, o seguinte começa
(não depois de uma parada ou de um erro, nem enquanto *Pausar tudo* segura a fila). Com *Iniciar quando houver
núcleos livres*, o seguinte começa ao lado dos que estão em jogo assim que o orçamento de núcleos passa do que eles
podem usar, cada um no seu paralelismo. Eles podem ser reordenados ou removidos, e o primeiro pode ser iniciado agora
quando nada está rodando. A página inicial diz quando o torneio em andamento termina (*Termina por volta de HH:MM (em
…)*) e quando a fila inteira termina, cada torneio da fila com a duração e, enquanto as durações anteriores são
conhecidas, quando ele começa; os torneios cuja duração não pode ser conhecida (um SPRT sem máximo, nós ou
profundidade por lance) são contados à parte.

**Relançar com…** num match terminado (no cabeçalho dele, ou em *Últimos concluídos* na página inicial) o joga de novo
com outro engine A, normalmente a versão seguinte, contra o mesmo engine B, com as outras configurações do match (as
da página *Novo torneio* para um match iniciado por uma versão anterior, que não as guardava).

**Clonar** (no cabeçalho de um torneio, em *Últimos concluídos* e em *Todos os torneios* na página inicial) abre a
página *Novo torneio* preenchida com a configuração desse torneio, seja qual for o formato: os participantes com as
configurações próprias deles, o controle de tempo, as aberturas, a adjudicação e o resto, cada valor como foi jogado.
Os perfis dele aparecem pelo nome, e um perfil alterado desde então é marcado como mudado para este torneio. Mude o que
deve ser diferente (outro controle de tempo, a próxima versão de um engine, outro livro) e depois **Iniciar** ou
*Adicionar à fila*. Um torneio ainda em andamento também pode ser clonado; um iniciado por uma versão anterior, que não
guardava a configuração, não.

## Ratings
<!-- en: ratings 3e4be48f -->

A página **Ratings** calcula ratings de máxima verossimilhança (o modelo logístico, empates como meio ponto, como o
Ordo faz por padrão) sobre todas as partidas de todos os torneios do perfil, e dos arquivos PGN importados. Os
engines são identificados pelo nome. As partidas contadas podem ser restringidas no topo da página, para todas as
abas:

- um controle de tempo, um grupo da biblioteca de engines (as partidas entre os engines dele), os últimos 30 ou 365
  dias;
- *Partidas importadas*: as partidas dos arquivos PGN listados em *Arquivos PGN…* (o download de uma lista de
  rating, as partidas de outro programa), lidas dos cabeçalhos; os engines delas são reconhecidos pelos nomes que o
  PGN lhes dá, e um nome que é o de um engine da biblioteca, sem contar maiúsculas e espaços ("stockfish  17" para
  "Stockfish 17"), é esse engine: as partidas dele contam como as do engine e o Elo de referência dele pode
  ancorá-las. Nada mais aproximado é aplicado sozinho: "Stockfish 17 64-bit" continua um engine separado até você
  dizer qual engine ele é em *Jogadores PGN…* (ao lado da contagem de engines da biblioteca, ou em *Arquivos PGN…*).
  Essa janela lista os jogadores que não são um engine da biblioteca pelo nome, com uma busca: para cada um, *Uma
  engine à parte* (o padrão) ou um engine da biblioteca. Um nome que dois engines da biblioteca compartilham, sem
  contar maiúsculas, aparece na cor de aviso para você escolher; um engine provável (o nome dele começa o do jogador)
  é sugerido como *Talvez …?*, nunca aplicado. A janela abre sozinha depois que um arquivo PGN é adicionado, quando há
  algo a decidir. As escolhas, incluindo a de um engine à parte, são guardadas com o engine (elas acompanham uma
  renomeação) e valem para todo arquivo PGN e para as listas de rating importadas para o Elo de referência; as
  partidas são relidas com elas quando a janela é fechada. A lista de rating diz quantos engines da biblioteca os
  arquivos PGN contêm, e a dica dela, quais;
- *Âncora*: a média em 0, ou um engine num rating fixo (por padrão, o primeiro engine com um Elo de referência).

### Lista de rating
<!-- en: rating-list c726b55e -->

A lista, com um número mínimo de partidas. Além do Elo, do ± e das partidas, cada engine mostra a pontuação, as
vitórias (+), os empates (=) e as derrotas (−), a taxa de empates, *Adv.*: o rating médio dos adversários que
enfrentou, partida a partida, *LOS*: a probabilidade de ele ser mais forte que o engine seguinte da lista, a partir
dos dois ratings e da covariância dos erros deles (aproximada acima de 1000 engines, como diz a dica), e *Diferença*:
o Elo dele acima do engine seguinte. Um clique no cabeçalho de uma coluna ordena por ela (# mantém a posição por
rating); um clique direito mostra ou oculta colunas (*Diferença* começa oculta). Um clique num engine abre a ficha
dele.

*Copiar* como texto ou BBCode, com *Com cabeçalho* para pôr acima da lista o cabeçalho descrito abaixo.
*Exportar…*: CSV com todas as colunas; uma página HTML (clara ou escura) e Markdown (uma tabela que o GitHub e os
fóruns mostram), as duas começando com um cabeçalho que diz como a lista foi feita: os torneios e os arquivos PGN
contados, as partidas com as vitórias das Brancas e das Pretas, os empates e a pontuação das Brancas, a âncora, os
filtros, o número mínimo de partidas, a data e a versão; e *Partidas para Ordo…*: as partidas contadas como um PGN só
de cabeçalhos, que o Ordo e o BayesElo leem (`ordo -p ratings.pgn -o ratings.txt`).

### Ficha do engine
<!-- en: engine-card 80442e81 -->

A ficha de um engine: o rating, os resultados no total e por controle de tempo, as perdas por tempo, os crashes e os
lances ilegais (cada partida abre no seu torneio), os torneios dele (cada um abre), a profundidade média, o tempo por
lance e o NPS lidos dos comentários dos lances, e o rating depois de cada torneio. A página *Engines* leva a ela com
*Resultados dele…*.

### Confronto direto
<!-- en: head-to-head 1021ba2f -->

Dois engines um contra o outro: o placar, o Elo e o LOS, as contagens pentanomiais dos pares de que eles jogaram as
duas partidas, por controle de tempo e por torneio, as partidas deles (cada uma abre no seu torneio) e *Exportar as
partidas…*.

### Progresso
<!-- en: progress e2ea28fc -->

O rating dos engines marcados depois de cada torneio que jogaram, num eixo de tempo: as versões de um engine lado a
lado.

## Transmissão
<!-- en: broadcast c2f489e7 -->

A aba *Transmissão* de um torneio em andamento o publica enquanto ele é jogado; o instantâneo é atualizado duas vezes
por segundo. *Partida mostrada* escolhe a partida enviada ao TLCS e à página web: automática, ou a escolhida na aba
*Ao vivo*.

### Arquivos ao vivo
<!-- en: live-files 09341896 -->

Gravados de forma atômica numa pasta de sua escolha, para publicar em qualquer lugar (FTP, rsync, um site):
`live.pgn` (as partidas em andamento), `standings.html` (classificação e tabela cruzada), `tournament.json` (tudo,
para scripts).

### Servidor TLCS
<!-- en: tlcs-server 712f28cb -->

Porta UDP, 16001 por padrão: o protocolo do Tom's Live Chess Server, para que o TLCV e outros visualizadores TLCS
possam acompanhar o torneio (tabuleiro, relógios, PV, tabela cruzada e as partidas jogadas). Ele é testado com o
node-tlcv. *Uma porta por partida* coloca cada partida simultânea numa porta própria: a partida #1 da aba *Ao vivo*
na porta UDP, a #2 na seguinte, e assim por diante. Cada porta abre quando a partida dela começa, com os próprios
limites de clientes e logons, e recebe a tabela cruzada depois de cada partida; *Partida mostrada* então não se
aplica.

### Página web
<!-- en: web-page 1d4c78b3 -->

Porta TCP: uma página servida pelo próprio fastchess-gui, para qualquer navegador da rede, somente leitura e
atualizada a cada segundo. A partida acompanhada (a primeira em andamento, ou a clicada) fica no meio: o tabuleiro com
o último lance, um rei em xeque, as coordenadas e as setas; o relógio de cada jogador em letras grandes; a avaliação
de cada engine em letras grandes com quem está pensando, a busca dele em blocos (profundidade/profundidade seletiva,
nós, velocidade, tempo, acessos às tablebases, hash) e a linha de jogo dele; e os lances, com os lances de livro em
itálico. Percorra os lances com as setas sob o tabuleiro ou as teclas ← → (▶ depois do último lance volta à partida
em curso), gire o tabuleiro com ⇅ ou F, e copie o PGN da partida ou o FEN da posição.

Ao lado da partida, abas mostram a classificação e a tabela cruzada, os gráficos da partida acompanhada, a busca
média por lance de cada engine com as anomalias dela, as próximas partidas do calendário (um suíço ou um mata-mata
emparceira as rodadas seguintes à medida que avança), as estatísticas e anomalias das partidas jogadas, e o evento.
Todas as partidas em andamento são mostradas abaixo, com uma pequena barra de avaliação ao lado de cada jogador.
*Moves* alterna os lances entre letras (Nf3) e figuras (♘f3), e o tema segue o sistema ou é fixado em claro ou
escuro; o navegador lembra essas escolhas. A página não carrega nada de outro lugar e funciona em celulares.
*Download the games (PGN)* dá o arquivo PGN do torneio, até a última partida registrada. `state.json`, que a página
lê, contém tudo isso para scripts. Os logos dos engines (veja [Aparência](#aparência)) aparecem ao lado dos nomes,
como pequenas imagens PNG que o programa gera a partir deles e serve ele mesmo; a página só conhece os dos engines do
torneio.

### lichess
<!-- en: lichess 7597e86f -->

Uma transmissão do lichess, com o tabuleiro, a barra de avaliação e o chat do lichess, para qualquer pessoa na
internet. Ela precisa de um token de API pessoal com o escopo `study:write` (*Criar um token no lichess…*), guardado
só em `settings.json` no perfil, salvo ao sair do campo. Sem uma rodada indicada, é criada uma transmissão com o nome
do torneio, não listada a menos que *Listada entre as transmissões do lichess* esteja marcado; o link dela é mostrado
assim que ela entra no ar. Do contrário, as partidas vão para a rodada cuja URL ou id é dado (uma transmissão
alimentada por envios, não por uma URL de origem). As partidas em andamento são enviadas a cada 8 segundos com os
relógios, e cada partida terminada uma vez; uma rodada comporta 64 partidas, depois do que é criada a rodada seguinte
da mesma transmissão. Só as partidas terminadas depois que ela foi ligada são enviadas; `lichess.json` na pasta do
torneio lembra para onde elas foram, então um torneio retomado continua a mesma transmissão. O lichess só guarda os
relógios do que recebe (ele descarta o `[%eval]` e o texto dos comentários dos lances): a barra de avaliação é a
dele, e as avaliações dos engines não aparecem lá.

A conexão usa o Mbed TLS, embutido no programa, e só confia nos certificados raiz que assinam o lichess.org (Google
Trust Services) e nos do Let's Encrypt. Ela passa pelo proxy HTTP que `HTTPS_PROXY` (ou `ALL_PROXY`) indica, como
`http://proxy.example:3128` ou `http://user:password@proxy.example:3128`, exceto para os hosts de `NO_PROXY`; também
no Windows o proxy vem dessas variáveis, não das configurações do sistema. Proxies SOCKS não são suportados.

### Um match por equipes no ar
<!-- en: a-team-match-on-the-air fd32027f -->

Um match por equipes mostra o placar das equipes em toda parte: no cabeçalho da página web e acima da classificação
dela (com a pontuação % da equipe A, Elo ± e LOS), em `standings.html` (as equipes, depois os engines de cada equipe
contra os da outra), em `tournament.json` e `state.json` (`teams`, `team_elo`, `team_elo_error`, `team_los`), e na
tabela cruzada do TLCS, como uma linha sob a tabela ("Lynx 17½ - 18½ Otter · 48.6 % · Elo -9.7 ± 31.2"). As partidas
dele levam as tags `WhiteTeam` e `BlackTeam` nos arquivos PGN, que as exportações mantêm.

### Pôr a página web na internet
<!-- en: putting-the-web-page-on-the-internet b19f02f8 -->

A página web pode ser acompanhada de qualquer lugar sem abrir uma porta no roteador: marque *Publicar na internet* em
*Página web* na aba *Transmissão*. Enquanto um torneio roda, o fastchess-gui abre um túnel SSH (libssh2, embutida no
programa) para um serviço de túnel, que dá à página um endereço https público; não é preciso instalar nada.

- *localhost.run* (o padrão): gratuito, sem conta. Os endereços dele mudam de vez em quando, e a velocidade é
  limitada; a página e o estado dela são enviados comprimidos, então isso não importa muito. A chave de host dele é
  conhecida do fastchess-gui: uma conexão com qualquer outra coisa é recusada.
- *Outro servidor SSH*: o host, a porta e o usuário dele, e a porta que ele abre para a página (como
  `ssh -R REMOTE_PORT:localhost:PORT user@host` pediria; 0 deixa o servidor escolher). Só podem ser usados servidores
  que deixam o usuário entrar sem chave nem senha (autenticação "none"), e eles precisam oferecer curve25519-sha256,
  uma chave de host RSA de 2048 a 4096 bits, e AES (GCM ou CTR). A chave do servidor é aceita na primeira vez e
  guardada no perfil (`known_hosts.json`); se ela mudar, o túnel para e avisa, e *Confiar na nova chave* serve para
  quando o dono do servidor confirma que ela mudou.

A aba mostra o estado (conectando, no ar com o número de conexões, reconectando, ou um erro) e, quando conhecido, o
endereço público com *Copiar*, *Abrir* (para ver a página como os espectadores a veem) e um QR code para um celular
(clique nele para ampliar). O túnel se reconecta sozinho depois de uma falha, esperando um pouco mais a cada vez, e
mostra o novo endereço se ele mudou. Ele para com a página web ou com o torneio.

Outros túneis também funcionam, iniciados separadamente: com o da Cloudflare, por exemplo, ligue a página web (porta
8080 aqui) e rode `cloudflared tunnel --url http://localhost:8080`; ele mostra um endereço como
`https://some-words.trycloudflare.com`. O ngrok, o tailscale funnel ou um proxy reverso como o nginx, também sob um
caminho próprio (`location /live/ { proxy_pass http://localhost:8080/; }`), funcionam do mesmo jeito.

A página é somente leitura: ela só responde a requisições GET da página, do estado dela e do arquivo PGN, nunca de um
arquivo que uma requisição nomeie, e nada nela altera o torneio. Cada requisição é respondida separadamente, no máximo
32 ao mesmo tempo, e precisa chegar em até um segundo e ser respondida em até dez, então um visitante lento ou calado
não segura ninguém; atrás do túnel, a conexão de cada espectador conta como uma delas: 40 são retransmitidas ao mesmo
tempo, outras tantas esperam a vez, e as seguintes são recusadas; um espectador que envia mais do que a página aceita
é desconectado sozinho. Dois downloads do arquivo PGN rodam ao mesmo tempo, e o seguinte é convidado a voltar um
instante depois. O estado nunca é guardado em cache, nem pelo túnel, então os espectadores veem as partidas à medida
que avançam; a página é guardada pelo navegador e verificada de novo a cada carregamento. Os espectadores mostrados na
aba *Transmissão* são contados a partir das requisições que as páginas fazem, então a contagem está certa também
atrás de um túnel.

Os espectadores veem o nome do torneio, os nomes dos engines, os ratings, as buscas e as anomalias deles, as aberturas
e o arquivo PGN das partidas jogadas. As pastas e os arquivos dos engines continuam privados. Em modo headless:

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Onde os espectadores conversam
<!-- en: where-the-viewers-talk eb3ef90c -->

A página não tem chat próprio; ela aponta para onde a conversa acontece. Um *Link do chat* em *Página web* (um
convite do Discord, um canal da Twitch, um tópico de fórum; só https, qualquer outra coisa é recusada com uma mensagem
sob o campo) coloca no cabeçalho da página um botão *Chat* que o abre numa nova aba. Enquanto as partidas são enviadas
ao lichess, a página também tem um link para a rodada do lichess, que tem o próprio chat.

Um *ID do servidor do Discord* acrescenta uma aba *Discord* ao lado de *Event*, com o widget de servidor do próprio
Discord no tema claro ou escuro da página: o nome do servidor, os membros online, os canais de voz e um botão
*Entrar*, não as mensagens. O dono do servidor o ativa em Configurações do servidor, Widget ("Ativar widget do
servidor"), que também mostra o ID do servidor para copiar (17 a 20 dígitos); com o Modo desenvolvedor ligado, um
clique direito no ícone do servidor também dá *Copiar ID do servidor*. O widget só é carregado de discord.com quando
um espectador abre a aba, e o Discord então vê a visita desse espectador (o endereço, o navegador, o login do Discord
se ele tiver um); sem um ID, a página não contata ninguém além do fastchess-gui. Os links do chat e do lichess não
enviam nada até um espectador clicar neles, e então nenhum referrer. Um arquivo de configuração rodado em modo
headless (`--run SETUP.json`) pega os dois de `web_chat_link` e `web_discord_id`.

## Modo headless
<!-- en: headless-mode 3471fbb3 -->

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Nenhuma janela é aberta; a versão, depois o progresso, são mostrados no console, com as tentativas de um engine que
não consegue iniciar, o engine posto de lado (com o log do engine) ou de volta, como na janela, e a classificação a
cada 20 partidas e no fim (um match por equipes: os placares das equipes, depois os engines de cada equipe contra os
da outra), a de um torneio terminando com a linha das cópias da classificação (partidas, empates, controle de tempo,
livro e evento). **Ctrl+C** para depois das partidas em andamento, e o torneio pode ser retomado mais tarde, pela
linha de comando ou pela interface gráfica. Códigos de saída: 0 terminado, 1 erro durante o jogo, 2 nada para rodar,
3 parado antes do fim (por Ctrl+C, ou sem nada restante para jogar sem um engine que não consegue iniciar). O
orçamento de memória e as GPUs das **Preferências** do perfil também se aplicam a ele.

`--detach` o joga em segundo plano: o comando retorna imediatamente, e o torneio continua sem terminal; a janela o
mostra (ela se conecta a ele) quando abre com o mesmo perfil, e `--attach` conversa com ele:

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

A janela comanda cada torneio do mesmo jeito: `--run FOLDER --control socket` (ou `--control stdio`) o joga sem
mostrar nada, trocando linhas JSON com a janela: os eventos do torneio (o estado, cada linha do diário, as partidas em
andamento lance a lance, cada partida registrada, o log, o fim) e os comandos da janela (pausar, retomar, parar,
alterações, o fim de uma partida, a parcela da máquina com `--allowance THREADS:MEMORY_MB:GPUS:ONE`). A primeira
linha em cada sentido é um hello, `{"hello": {"protocol": 1, ...}}`; um script via `--attach` recebe o hello e os
eventos do torneio, e pode enviar comandos como `{"c": "pause"}`. Uma janela (ou um script) por vez conversa com um
torneio; `--attach` o solta quando a entrada padrão dele termina ou a saída dele não é mais lida, de modo que um
script mantenha a entrada aberta enquanto lê as respostas.

O torneio pode ser transmitido ao mesmo tempo, como pela aba *Transmissão*:

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` pega o token da variável de ambiente `FCG_LICHESS_TOKEN`, ou o do perfil.

As opções podem ser combinadas. Um arquivo de configuração salvo com a transmissão ligada transmite do mesmo jeito
sem elas. Para espectadores fora da rede local, abra as portas no roteador: o TLCV e o node-tlcv se conectam à porta
TLCS. Para a página web, um túnel evita abrir uma porta (veja [Pôr a página web na
internet](#pôr-a-página-web-na-internet)).

## Máquinas
<!-- en: machines 4d8fbfef -->

Outros computadores para rodar torneios: uma máquina de testes, um servidor, um segundo PC. **Preferências** >
*Máquinas* os lista. Uma máquina é acessada por SSH: nada precisa ser aberto nela além do servidor SSH, e o programa
não guarda nenhum segredo.

### Adicionar uma máquina
<!-- en: adding-a-machine f1f15823 -->

*Adicionar uma máquina*, depois na ficha dela:

- *Nome*, *Host* (o nome dela na rede, ou o endereço), *Porta* (22) e *Usuário*, a conta com que os torneios dela
  rodam;
- *Entrar com*: o *Agente SSH* (ssh-agent no Linux e no macOS, Pageant ou o serviço *OpenSSH Authentication Agent*
  no Windows; adicione a sua chave a ele com `ssh-add`), ou um *Arquivo de chave*, RSA ou Ed25519
  (`~/.ssh/id_ed25519`). A frase-senha de um arquivo de chave é pedida uma vez por sessão: ela fica na memória e nunca
  é gravada em lugar nenhum. A máquina precisa conhecer a chave: a metade pública dela (`id_ed25519.pub`) vai em
  `~/.ssh/authorized_keys` na máquina (`ssh-copy-id user@host` a coloca lá), ou, para um administrador de uma máquina
  Windows, em `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Pasta lá*: a pasta do fastchess-gui na máquina, para o programa, os torneios, os livros e as tablebases, relativa
  à pasta pessoal do usuário (`fastchess-gui` por padrão) ou absoluta (`/srv/fcg`, `D:\fcg`);
- *Orçamento*: os núcleos e a memória que os torneios da máquina podem usar ao mesmo tempo; 0 pega o que o teste
  encontrou, todas as threads menos uma e 80 % da memória.

*Testar a conexão* entra na máquina, depois informa o sistema, as threads, a memória, o espaço livre onde está a
pasta dela, e se o fastchess-gui está instalado lá, em qual versão. A primeira conexão mostra a chave de host da
máquina (a impressão digital SHA256 dela) e confia nela a partir daí (`known_hosts.json` no perfil, o mesmo do túnel
da transmissão). Quando a chave muda mais tarde, nada é enviado e a ficha avisa, com *Confiar na nova chave* para uma
máquina que foi reinstalada. Toda falha nomeia a máquina e o que fazer: o host que não pode ser alcançado, uma chave
que a máquina recusa (e para onde vai a metade pública dela), uma frase-senha errada, nenhum agente rodando.

*Instalar o fastchess-gui* (*Atualizar o fastchess-gui* depois que ele está lá) copia o programa para a pasta da
máquina por SFTP, depois verifica se ele inicia lá (`fastchess-gui --version`). Ele copia este programa quando a
máquina roda o mesmo sistema (Linux x86-64 para Linux x86-64); para outro, uma máquina Windows a partir do Linux, por
exemplo, baixe a versão para esse sistema e escolha o programa dela na ficha (*fastchess-gui para Windows x86_64*).
Uma cópia interrompida pela rede continua de onde parou.

### Engines numa máquina
<!-- en: engines-on-a-machine 4b00e462 -->

*Motores nesta máquina*: os engines da biblioteca que a máquina tem, cada um com o caminho lá
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Verificar os motores* verifica cada caminho: o programa está lá,
pode ser executado e responde a `uci` (`xboard` para um engine WinBoard) em até 10 segundos. A marca ao lado, e a
dica dela, dizem o que foi encontrado: o nome do engine, ou por que ele não roda (ausente, não executável, feito para
outro sistema, mudo).

Um engine ausente na máquina é copiado para lá com *Copiar…* na linha dele (ou *Copiar \<engine\> para \<máquina\>* na
paleta de comandos, ou o botão de um início recusado porque um engine não tem caminho lá). O painel que abre diz
primeiro o que vai: o programa, o tamanho e o sistema para o qual ele foi compilado, lido dos primeiros bytes dele (um
programa Windows, um Linux ou macOS, universal ou não, ou um script). Um programa que a máquina não pode rodar é
recusado antes de qualquer envio, dizendo qual compilação escolher: "Alpha é um programa Windows; tc-box roda Linux:
escolha a compilação Linux de Alpha". A arquitetura também conta (x86-64 ou arm64); uma compilação x86-64 num Mac
arm64 só roda sob o Rosetta, e um formato que o programa não conhece é copiado mesmo assim: os dois casos são
avisados, não recusados. Um script só vai para uma máquina Linux ou macOS, e só quando o interpretador da primeira
linha dele (`python3`, `/bin/sh`) está lá. *Escolher a compilação \<sistema\> de \<engine\>…* pega outro arquivo deste
computador, a compilação Windows do engine, por exemplo, verifica-o do mesmo jeito, e o guarda para aquela máquina: as
comparações e substituições seguintes o usam, não o programa da biblioteca.

*Com a pasta* copia a pasta inteira do engine junto com o programa (a pasta de trabalho dele, ou a pasta do
programa), para um engine que precisa de arquivos ao lado: as redes, um livro. O painel conta primeiro os arquivos e
o tamanho deles, e nomeia as opções do engine que apontam para dentro dela (`EvalFile` de uma rede): num torneio na
máquina, elas apontam para a cópia lá, e a pasta é a pasta de trabalho do engine. Uma pasta de mais de 2000 arquivos
não é oferecida (um programa em `/usr/bin` não deve ser copiado com a pasta), uma pasta `.git` fica de fora, e para
uma máquina Windows um nome de arquivo que o Windows não aceita (`:`, `*`, `?`, um ponto no fim, `CON`, `NUL`...) é
nomeado e impede a cópia antes de ela começar. Os arquivos são copiados como estão, finais de linha incluídos.

*Copiar* envia os arquivos por SFTP para `engines/<engine>/` na pasta da máquina, em segundo plano com o tempo
restante e *Cancelar*, retomado de onde parou depois de uma interrupção; o SHA-256 de cada arquivo é comparado com o
deste computador depois da cópia, e os arquivos que já estão lá e são iguais não são enviados de novo. No Windows, o
programa mantém ou recebe o `.exe`; nos outros sistemas, ele se torna executável (`chmod 755`). Depois, o caminho do
engine na máquina é definido como a cópia e verificado como *Verificar os motores* faz. Num Mac, um programa copiado
de outro computador que não inicia pode precisar de uma assinatura ou de sair da quarentena: a mensagem dá os dois
comandos a rodar lá (`codesign -s -` e `xattr -d com.apple.quarantine`); o programa não os roda por conta própria.

A coluna *Compilação* compara o engine de lá com o deste computador, pelo SHA-256 (*Verificar os motores* e
*Comparar* fazem isso, com o hash do programa deste computador calculado uma vez até ele mudar): *mesma compilação
que este computador*, ou *outra compilação lá*, com *Substituir pelo deste computador*. Substituir copia o programa
deste computador para a pasta do engine na pasta da máquina e aponta o engine para lá: um programa em outro lugar da
máquina (`/opt/engines/stockfish`) nunca é sobrescrito, ele fica como estava.

### Livros e tablebases numa máquina
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Verificar o que a configuração usa* compara o livro de aberturas e os livros próprios dos participantes da
configuração com a pasta de livros da máquina, por tamanho e SHA-256 (um livro com o mesmo nome mas outro conteúdo não
é o mesmo livro), e os arquivos Syzygy da configuração de no máximo 6 peças com a pasta de tablebases da máquina, por
nome e tamanho. *Enviar os arquivos ausentes* copia o que falta ou é diferente, em segundo plano, com o tempo
restante; cada livro é verificado pelo hash depois de copiado, cada tabela pelo tamanho, e uma cópia interrompida é
retomada. As tabelas de 7 peças não são oferecidas: elas ocupam terabytes. Um torneio não começa numa máquina sem o
livro dele; tablebases ausentes são um aviso (a adjudicação e os engines se viram sem elas).

### Um torneio numa máquina
<!-- en: a-tournament-on-a-machine 8f918d9a -->

Na configuração, *Jogar em* (em *Paralelismo*) escolhe onde o torneio é jogado: *Este computador*, ou uma máquina.
*Iniciar* então o prepara lá: o fastchess-gui precisa estar instalado lá nesta versão (senão, *Atualizar o
fastchess-gui*), todo engine precisa ter o caminho dele na máquina (senão, o início oferece *Copiar \<engine\> para
\<máquina\>…*), e os livros da configuração precisam estar na pasta de livros dela (senão, o início diz qual falta, e
*Enviar os arquivos ausentes* o coloca lá); tablebases ausentes são só um aviso. O torneio recebe a sua pasta em
`tournaments/` da pasta da máquina, com os engines, os livros e as tablebases nos caminhos deles lá, e o processo dele
inicia lá fora da sessão SSH (`fastchess-gui --run <folder> --detach`): ele continua jogando aconteça o que acontecer
com a janela ou com a rede. A janela o acompanha como um torneio deste computador: Ao vivo, Partidas, a
classificação, pausar, parar e as alterações, pelo canal de controle do processo dele
(`fastchess-gui --attach <folder>` via SSH).

O cartão dele na página inicial diz *Em \<máquina\>*, e *Conectando a \<máquina\>…* enquanto a conexão está cortada: a
janela se reconecta sozinha, esperando um pouco mais depois de cada falha, e retoma os eventos de onde a cópia dela
parou. O diário e os arquivos PGN são copiados de volta para a pasta do torneio aqui à medida que crescem, a cada
poucos segundos, para que os Ratings, as exportações e o checklist do envio os leiam; a pasta da máquina continua
sendo a referência (`machine.json` aqui diz qual máquina e qual pasta). Fechar a janela o deixa jogando lá; a próxima
inicialização o mostra de novo. *Retomar* um torneio interrompido de uma máquina o retoma lá. O orçamento da máquina
(os núcleos e a memória dela) é dividido pelos torneios dela, à parte dos deste computador. Um torneio é jogado numa
só máquina: as partidas dele não são distribuídas entre várias.

Uma máquina precisa de um servidor OpenSSH (Linux, macOS, BSD; Windows 10 1809 e posteriores), com uma chave de host
RSA e SFTP, os dois presentes por padrão. Os comandos passam pelo `sh`, qualquer que seja o shell do usuário (fish ou
csh servem), e pelo cmd no Windows, o shell padrão do servidor OpenSSH dele.

### Engines jogados a partir deste computador
<!-- en: engines-played-from-this-computer 8c14dc16 -->

Um engine da biblioteca pode rodar numa máquina enquanto o torneio é jogado aqui: um engine de GPU na máquina com a GPU,
um engine compilado para outro sistema, uma máquina reservada para um engine. Na ficha dele, *Roda em* escolhe *Este
computador* ou uma máquina; *Programa lá* e *Pasta de trabalho lá* são então caminhos na máquina (vazio: a pasta do
programa lá), e os botões *…*, incluídos os das opções que nomeiam um arquivo, navegam pelos arquivos da máquina. Quando
há máquinas, *Adicionar…* oferece *Um programa em \<máquina\>…*, *Importar…* oferece *Todos os engines de uma pasta em
\<máquina\>…*, e o *Adicionar um motor…* da máquina oferece *Um programa de lá…*. O engine é lido, verificado e jogado
no console, no teste rápido e nos torneios como um engine deste computador. O caminho dele é também o da lista de
engines da máquina, e o SHA-256 do programa dele lá diz quando ele foi recompilado (Δ, lido de novo).

A máquina precisa ter sido testada uma vez (*Testar a conexão*), para que a chave de host dela seja conhecida: um engine
nunca se conecta a uma máquina cuja chave não foi mostrada e aceita como confiável. A frase-senha da chave, se ela tiver
uma, é pedida uma vez por sessão, antes da primeira leitura ou do início, e fica na memória: o processo do torneio a
recebe da janela e a guarda, para que o torneio continue jogando depois que a janela fecha. `--run` num terminal não a
pede: use o agente SSH lá.

Cada engine é iniciado por meio de um retransmissor, `fastchess-gui --remote-engine <key>`, servido pelo processo que o
joga: uma conexão SSH com a máquina para no máximo dez engines (o `MaxSessions` de um servidor OpenSSH), cada engine
iniciado a partir da pasta dele lá. Um engine cujo retransmissor termina é encerrado lá também, pelo id do processo
dele, quando não termina sozinho (numa máquina Linux ou macOS; numa máquina Windows, o canal dele é fechado).

*Verificar* mede o tempo de ida e volta até a máquina, a mediana de alguns `isready`. O relógio o conta: o resumo da
configuração avisa quando ele passa de 5 % do incremento, ou do tempo por lance quando não há incremento, com *Definir
uma margem de … ms* (o dobro da ida e volta). Uma máquina nunca testada impede o início, e o mesmo faz um torneio que
roda numa máquina diferente da do engine (um torneio que roda na máquina do engine o joga lá como um engine da própria
máquina).

Enquanto o torneio é jogado, a página inicial tem uma linha *Máquinas* abaixo dos núcleos: cada máquina, conectada e o
que joga lá, ou desde quando ela não responde. Uma máquina que não responde é um item em *Requer atenção*: as partidas
dos engines dela são postas de lado e tentadas de novo, as outras continuam, com *Tentar novamente* e *Testar
\<máquina\>*. Uma partida que um engine perdeu porque a conexão dele caiu (com o fim registrado como desconexão) é
anulada e jogada de novo, registrada no diário com o motivo (`connection to its machine lost: <machine>`); *Saúde* a
conta à parte, não como um crash do engine. Uma máquina desconectada mais de três vezes em uma hora tem os engines dela
postos de lado. Os núcleos, a memória e as GPUs deste computador não contam os engines em máquinas, só os
retransmissores deles; o orçamento da máquina, que os torneios dela dividem, ainda não os conta.

### Uma máquina Windows
<!-- en: a-windows-machine 58ae029a -->

O Windows 10 (1809 e posteriores) e o Windows 11 incluem um servidor OpenSSH, não instalado por padrão. No
PowerShell, como administrador:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

A instalação abre a porta 22 no firewall. Depois coloque a sua chave pública em
`C:\Users\<user>\.ssh\authorized_keys`, ou, para uma conta do grupo Administradores, em
`C:\ProgramData\ssh\administrators_authorized_keys`, que só os Administradores e o SYSTEM podem ler (os grupos são
indicados pelos SIDs, os mesmos em todos os idiomas do Windows):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Mantenha o cmd como shell do servidor: o fastchess-gui fala cmd, não PowerShell (se um valor `DefaultShell` foi
definido em `HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, remova-o). Na ficha da máquina, escolha o `fastchess-gui.exe` da
versão para Windows, depois *Instalar o fastchess-gui*.

## Arquivos
<!-- en: files 837f3661 -->

Tudo fica na pasta do perfil: `~/.local/share/fastchess/fastchess-gui/` no Linux,
`%APPDATA%\fastchess\fastchess-gui\` no Windows, `~/Library/Application Support/fastchess/fastchess-gui/` no macOS.
Os arquivos que o programa cria não podem ser alterados por outros usuários, e `settings.json` só pode ser lido pelo
dono assim que contém um token do lichess. **Preferências** mostra a pasta do perfil, com *Abrir a pasta*.

### Modo portátil
<!-- en: portable-mode 29ddbe3b -->

Com um arquivo chamado `portable` (vazio, sem extensão) ao lado do programa (ao lado de `fastchess-gui.exe` ou
`fastchess-gui`, ao lado de `fastchess-gui.app` no macOS), o perfil passa a ser a pasta `data` ao lado do programa, e
a pasta de torneios padrão é `data/runs`: nada é gravado em outro lugar, exceto nas pastas escolhidas para torneios,
exportações e afins. Os caminhos do que está na pasta do programa (engines, livros, tablebases, suítes de testes, os
arquivos PGN dos ratings e da configuração, as pastas escolhidas) são guardados em relação a ela nos arquivos do
perfil, então a pasta inteira pode ser movida, copiada para outra máquina ou rodada de um pen drive, qualquer que seja
a letra da unidade, e os engines dela são encontrados de novo. **Preferências** diz *Modo portátil* ao lado da pasta
do perfil. Para levar um perfil existente, copie os arquivos dele para `data`. `--profile` ainda escolhe outra pasta,
fora do modo portátil. Um torneio mantém os caminhos dos engines de quando foi criado: retome um torneio inacabado
antes de mover a pasta. No macOS, remova primeiro a quarentena (veja o README): do contrário, o macOS roda o
aplicativo a partir de um lugar temporário, onde o arquivo `portable` não está.

### A pasta do perfil
<!-- en: the-profile-folder 91838f38 -->

- `engines.json`: a biblioteca de engines, e `engines.json.bak` a versão anterior dela, lida se a primeira se perder;
- `setup.json`: a última configuração; `profiles.json`: os seus perfis das seções da configuração;
  `templates/*.json`: os modelos salvos; `queue.json`: a fila;
- `book-cache/`: os livros PGN transformados em livros Polyglot para os livros próprios dos participantes (removidos
  depois de 30 dias sem uso);
- `layout.ini`: o layout da janela e os painéis do espaço de trabalho; `layouts.json`: os layouts salvos por nome;
  `fastchess-gui.log`: o log da última sessão;
- `suite.json`: a última configuração da página Suítes; `suites/<suite>-<date>.json`: as execuções concluídas de
  suítes de testes;
- `board.json`: as configurações da análise e das partidas da página Tabuleiro; `analyses/*.json`: as partidas
  analisadas lance a lance, um arquivo cada; `games/*.pgn`: as partidas jogadas a partir da página Tabuleiro;
- `settings.json`: a pasta dos torneios, quando outra foi escolhida em **Preferências**, *Renderização por software*,
  a pasta de *Todos os engines de uma pasta…*, o testador e o local de *Exportar para uma lista de rating…*, o token
  do lichess, o tamanho e a posição da janela, a largura do painel do guia, o cartão de primeiros passos ocultado, a
  verificação de nova versão (desejada ou não, quando foi feita pela última vez, a versão que encontrou, o aviso
  ocultado), a aparência (cores do tabuleiro, peças, sons dos lances), e o orçamento da máquina (núcleos, memória e
  GPUs para os torneios, *Iniciar quando houver núcleos livres*);
- `logos/`: os logos dos engines, cópias das imagens escolhidas nas fichas deles;
- `machines.json`: as máquinas remotas de **Preferências** (o endereço, a pasta, os orçamentos, os caminhos dos
  engines e o que o último teste encontrou; nenhuma frase-senha), e `known_hosts.json` as chaves de host confiáveis,
  das máquinas e do túnel;
- `running.json`: os torneios em jogo agora, com os pesos, cada um removido quando para; encontrado ao iniciar, ele
  diz o que mostrar de novo (um torneio que continuou sendo jogado sem a janela) ou retomar;
- `runs/`: os torneios, a menos que outra pasta tenha sido escolhida (veja [A pasta de um
  torneio](#a-pasta-de-um-torneio)).

### A pasta de um torneio
<!-- en: a-tournaments-folder 76959723 -->

`runs/<date>_<A>_vs_<B>/` para um match, `runs/<date>_<name>/` para um torneio (em `runs/` ou na pasta escolhida),
uma pasta cada, que pode ser movida, copiada, compactada em zip e enviada:

- `tournament.json`: o formato, os participantes (engines copiados com as opções) e as condições;
- `setup.json`: a configuração da página *Novo torneio* com que ele começou, lida por *Clonar* e *Relançar com…*
  (veja [Fila e relançamento](#fila-e-relançamento)); um torneio iniciado por uma versão anterior não o tem;
- `journal.jsonl`: tudo o que aconteceu desde então, uma linha por evento (pares de partidas iniciados, partidas
  terminadas, alterações, emparceiramentos de suíço e de mata-mata); com os arquivos PGN, é o que uma retomada lê.
  Uma partida está no disco antes da sua linha, então uma queda de energia perde no máximo as partidas em andamento.
  O texto de partidas para o qual nenhuma linha aponta (uma partida descartada por uma alteração enquanto era salva,
  ou deixada por um crash) é substituído por linhas em branco, para que outros visualizadores de PGN só mostrem as
  partidas do torneio;
- `tournament.lock`: mantido pelo programa que joga o torneio; outro programa que abre a pasta só o mostra;
- `<name>.pgn`: comentários dos lances no estilo cutechess / fastchess, `{+0.35/18 1.234s}`, com a pontuação do
  ponto de vista do lado que jogou; as partidas na ordem em que terminaram (o diário encontra cada uma pela posição no
  arquivo; *Exportar tudo* na aba *Partidas*, e o arquivo de *Também adicionar cada partida a*, seguem o calendário);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, com a avaliação do ponto de
  vista das Brancas, como o ChessBase / Fritz e o Lichess a leem;
- `<name>` é o nome do torneio quando ele foi criado (renomeá-lo depois mantém os arquivos), com os caracteres que o
  Windows não permite substituídos por `_` e cortado em 64 bytes; `tournament.json` registra os dois nomes. Um
  torneio sem nome, um mais antigo, ou um match importado da 0.2.x tem `games.pgn` e `games-chessbase.pgn`;
- `lichess.json`: a transmissão e a rodada do lichess para as quais as partidas foram (sem token);
- `processor.tsv`: o tempo de processador que os engines de cada partida usaram, para o *Uso do processador* de um
  torneio retomado;
- `fastchess-gui.log`: o log dos processos que o jogaram (cada torneio é jogado num processo próprio);
  `control.json`, enquanto um processo o joga: onde está o socket local dele (`control.sock` na pasta, ou na pasta de
  execução do usuário quando esse caminho é longo demais; um named pipe no Windows), e qual processo escuta;
- `logs/game-N.log`: cada linha trocada com os dois engines durante a partida N (o número mostrado na aba
  *Partidas*), com a hora em que foi enviada ou lida (`W>` enviada ao engine das Brancas, `W<` lida da saída dele,
  `W!` lida da saída de erro dele: asserções, mensagens de crash), para as partidas perdidas por tempo, por crash, por
  travamento ou por lance ilegal (ou todas as partidas, se pedido). As 300 primeiras linhas de cada engine e as 5000
  últimas linhas da partida são mantidas, na ordem em que chegaram. É o que o autor de um engine precisa para
  reproduzir um bug. Para um engine WinBoard, ele mostra o lado UCI; `FCG_WB2UCI_LOG` dá o lado WinBoard;
- `logs/start-<engine>.log`: a última tentativa com falha de iniciar um engine que não consegue iniciar: o programa,
  o erro, e as linhas trocadas com os engines daquela partida até então (veja [Um engine que não consegue
  iniciar](#um-engine-que-não-consegue-iniciar)).

Uma pasta recebida de outra pessoa pode ser aberta com segurança: o arquivo de *Também adicionar cada partida a*
precisa ser um arquivo `.pgn` (outro nome é ignorado, com um aviso), e uma pasta cujo diário, arquivos PGN ou arquivo
de bloqueio são links simbólicos não é aberta para gravação, então ela não pode fazer o programa gravar fora dela.
Retomá-la roda os engines que o `tournament.json` dela nomeia, como qualquer torneio faz.

Os matches jogados com a versão 0.2 aparecem na página inicial: *Importá-los* os transforma em torneios concluídos.

## Linha de comando
<!-- en: command-line bba4b98e -->

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH] [--software]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
              [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
              [--control stdio|socket] [--detach] [--allowance THREADS:MEMORY_MB:GPUS:ONE]
fastchess-gui --attach TOURNAMENT_FOLDER
fastchess-gui --version
```

- `--profile DIR` usa outra pasta de perfil;
- `--autostart` inicia a configuração salva ao iniciar (em vez de retomar o torneio que estava em andamento, veja
  [Pausar, parar e retomar](#pausar-parar-e-retomar));
- `--size WxH` abre a janela nesse tamanho, centralizada, em vez de como foi deixada, e não mexe no tamanho e na
  posição guardados para a próxima inicialização;
- `--software` faz o processador desenhar a janela (o renderizador por software do SDL) em vez do OpenGL, como
  *Renderização por software* em **Preferências** faz, para esta execução (veja [Renderização da
  janela](#renderização-da-janela));
- `--shot` salva uma captura de tela da janela depois do número de segundos dado (5 por padrão), com WINDOW trazida
  para a frente, e sai depois que todas as capturas forem feitas. Pode ser repetido. WINDOW é uma página (`home`,
  `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`,
  `settings`) ou uma aba (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). O mouse é
  ignorado nesse modo e, no Linux, a janela é renderizada fora da tela (nenhuma janela aparece; defina
  `SDL_VIDEODRIVER` para mudar isso); a janela abre no tamanho padrão, ou `--size`, e o tamanho e a posição guardados
  não são alterados;
- `--focus WINDOW` abre essa página ou aba ao iniciar, ou: `engines-card` (a ficha do primeiro engine),
  `engines-refresh` (*Atualizar: os engines novos da pasta*), `engines-ratings=FILE` (a prévia dessa lista de
  rating), `setup-picker` e `setup-suggest` (o filtro da biblioteca com o foco, ou os adversários sugeridos
  selecionados), `setup-open=ROW` e `setup-profiles=ROW` (uma linha da configuração aberta, ou a lista de perfis
  dela: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (o livro
  próprio do primeiro participante), `submission-export` (*Exportar para uma lista de rating…*), `ratings-players` (a
  janela *Jogadores PGN*), `clone=DIR` (a página *Novo torneio* preenchida por *Clonar* a partir da pasta desse
  torneio), `palette[=TEXT]` (a paleta de comandos, com TEXT digitado nela), `game-menu=N` (o menu da
  partida N na aba *Partidas*, com um resultado escolhido), `end-menu=N` (*Encerrar com um resultado* da partida do
  slot N, a partir de 1), `big-screen[=N]` (a tela grande, da partida do slot N), `live-back=N` (a partida
  acompanhada em *Ao vivo*, N meios-lances para trás), `engines-console` (o console do primeiro engine),
  `board-analyse` (a página Tabuleiro, com análise), `board=FILE` (um arquivo PGN, FEN ou EPD na página Tabuleiro;
  `board-analyse=FILE` analisado, `board-review=FILE` a primeira partida dele analisada lance a lance), `board-edit`
  (o editor de posição), `board-play` e `board-play-start` (uma partida entre os dois primeiros engines, iniciada),
  `machines` (a ficha da primeira máquina remota em **Preferências**; `machines-test` testada, `machines-files` os
  livros e as tablebases da configuração comparados com ela), `guide[=ID]` (o painel do guia, na seção ID),
  `guide-search=TEXT` (o painel do guia, buscando TEXT);
- `--select-game N` abre a partida N (na ordem de término) na aba *Partidas* assim que ela existir;
- `--quit-after SECONDS` para o torneio e sai;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: veja [Modo headless](#modo-headless);
- `--version` mostra a versão e o protocolo do canal pelo qual a janela acompanha um torneio remoto (veja
  [Máquinas](#máquinas)), e sai.

## Solução de problemas
<!-- en: troubleshooting e8c4882d -->

### O programa não inicia
<!-- en: the-program-does-not-start dc0a0128 -->

- **O Windows diz que o programa não é seguro, ou um antivírus o remove.** O programa não é assinado, então o Windows
  SmartScreen e alguns antivírus desconfiam dele. *Mais informações*, *Executar assim mesmo* o deixa iniciar; um
  antivírus pode ser configurado para permitir a pasta dele. As versões são compiladas pelo GitHub Actions a partir do
  código-fonte público.
- **O macOS se recusa a abri-lo.** Ele não é assinado por uma conta de desenvolvedor Apple: clique nele com Control
  (clique direito), Abrir, depois Abrir de novo; no macOS 15 e posteriores, tente abri-lo uma vez, depois Ajustes do
  Sistema, Privacidade e Segurança, Abrir Mesmo Assim. Ou rode `xattr -dr com.apple.quarantine` na pasta
  descompactada.
- **Uma mensagem diz que nenhuma forma de desenhar a janela funciona, ou a janela fica preta.** O driver gráfico não
  tem OpenGL 3.2 ou falha: inicie com `--software` (o processador desenha a janela) e, se funcionar, marque
  *Renderização por software* em **Preferências**. No Wayland sem driver gráfico, inicie com `SDL_VIDEODRIVER=x11`.
  Veja [Renderização da janela](#renderização-da-janela).
- **O texto chinês aparece como quadrados.** O chinês precisa de uma fonte do sistema: Microsoft YaHei no Windows,
  Noto Sans CJK ou WenQuanYi no Linux (instale `fonts-noto-cjk` ou similar).

### Um engine não inicia
<!-- en: an-engine-does-not-start 202a67d4 -->

- **A verificação dele falha, ou o torneio dele diz que ele não consegue iniciar.** Leia o erro sob o engine, depois
  o log do engine (*Log do engine*, ou `logs/start-<engine>.log` na pasta do torneio): a saída de erro do engine
  muitas vezes nomeia o arquivo que falta. As causas habituais: o programa foi movido (*Localizar…*), uma rede ou uma
  biblioteca não encontrada (defina a pasta de trabalho, ou `EvalFile`, na ficha dele), um programa compilado para
  outro processador (uma compilação que usa AVX-512 numa máquina sem essa instrução), um início lento (aumente *Tempo
  limite de início* na ficha dele). Veja [Um engine que não consegue iniciar](#um-engine-que-não-consegue-iniciar).
- **Um engine WinBoard não começa das posições do livro.** Um engine de protocolo 1 sem `setboard` só pode começar
  da posição inicial: dê a ele um livro PGN (lances), não um livro EPD. Veja [Engines WinBoard](#engines-winboard).
- **O torneio se recusa a começar: um engine não joga Chess960.** Todo engine precisa ter a opção `UCI_Chess960`
  (UCI) ou a variante `fischerandom` (WinBoard); o resumo nomeia os que não têm.

### Partidas perdidas ou que parecem erradas
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Um engine perde por tempo no início das partidas.** Na maioria das vezes ele está mal configurado, e não jogando
  mal: threads demais para os núcleos que as partidas têm, um hash grande demais para alocar a tempo, uma rede
  carregada a cada partida, um início lento. Reduza as `Threads` e o `Hash` dele, desmarque *Reiniciar o engine a
  cada partida*, aumente *Tempo limite de início*; o cartão *Saúde* e o *TF* da visão geral mostram as perdas por
  tempo.
- **Os engines parecem mais lentos do que deveriam.** Partidas demais ao mesmo tempo: deixe o *Paralelismo*
  automático, e mantenha paralelismo × threads dentro dos núcleos físicos. *Uso do processador* na visão geral nomeia
  um engine que usa mais núcleos do que as suas `Threads`, ou que pensa no tempo do adversário.
- **O resumo avisa que a memória não vai bastar.** As tabelas de hash das partidas não caberiam na memória livre:
  reduza o `Hash` (em *Configurações dos engines*, ou o do engine que ele nomeia) ou o paralelismo.
- **Arquivos de tablebase são apontados como ausentes ou truncados.** Um download parou no meio, ou a pasta só
  contém tabelas menores: baixe de novo os arquivos nomeados na dica, ou reduza a adjudicação às peças encontradas
  (*Baixar para 5 peças*).
- **Um livro é apontado como tendo posições ilegais ou duplicadas.** O fastchess as deixa de fora; *Salvar uma cópia
  limpa…* na página Livros grava um livro sem elas. Veja [Verificar um livro](#verificar-um-livro).
- **Uma partida foi decidida de forma errada.** Clique direito nela em *Partidas* ou *Calendário*: jogá-la de novo,
  anulá-la, ou definir o resultado dela, com o motivo. Veja [Alterar um torneio em
  andamento](#alterar-um-torneio-em-andamento).

### A transmissão
<!-- en: broadcasting 4a2e6916 -->

- **O TLCV ou outro visualizador não se conecta.** Inicie o torneio (e o servidor TLCS dele) antes do visualizador:
  ele não refaz o logon sozinho. Abra a porta UDP no roteador para espectadores fora da rede local.
- **A página web não pode ser acessada de fora.** Marque *Publicar na internet* para um túnel (veja [Pôr a página web
  na internet](#pôr-a-página-web-na-internet)), ou abra a porta TCP no roteador.
- **O lichess recusa o token.** O token precisa do escopo `study:write`: crie um novo com *Criar um token no
  lichess…*. Atrás de um proxy, defina `HTTPS_PROXY`.

### Qualquer outra coisa
<!-- en: anything-else ce59c083 -->

*Relatar um problema…* em **Preferências** ou na página **Log** abre uma issue no GitHub já preenchida com a versão,
o sistema e as últimas linhas do log (veja [Relatar um problema](#relatar-um-problema)).

## Glossário
<!-- en: glossary 0386d0d9 -->

- **Adjudicação**: encerrar uma partida antes do mate quando o resultado é claro: empate quando as duas pontuações
  ficam perto de 0, derrota quando ficam muito abaixo, ou o que as tablebases dizem. Veja
  [Adjudicação](#adjudicação).
- **BayesElo, DrawElo**: um modelo de rating com um parâmetro separado para os empates (DrawElo), do programa de Rémi
  Coulom.
- **Buchholz, Sonneborn-Berger**: critérios de desempate: a soma das pontuações dos adversários (num suíço), e a soma
  das pontuações dos adversários vencidos mais metade das dos adversários com quem houve empate (num todos contra
  todos).
- **Cabeça de chave**: uma posição na ordem dos participantes: os engines em teste de um gauntlet, os favoritos de um
  mata-mata.
- **Chess960, Chess324**: posições iniciais com a fileira de trás embaralhada: 960 com o rei entre as torres (roque
  para as casas habituais), 324 com o rei e as torres no lugar.
- **Ciclo**: num todos contra todos, cada emparceiramento jogado uma vez com cada cor (o cutechess chama de rodada, de
  2 partidas por encontro).
- **ECO**: os códigos da Encyclopaedia of Chess Openings, de A00 a E99.
- **Elo, nElo**: a diferença de rating que uma pontuação representa. O nElo (Elo normalizado) divide pela dispersão
  dos resultados, para que o valor não dependa da taxa de empates; o fishtest e o OpenBench expressam nele os limites
  dos seus SPRT.
- **EPD, FEN**: uma linha que descreve uma posição; uma linha EPD pode trazer operações, como o melhor lance `bm` de
  uma suíte de testes.
- **Gauntlet**: os engines em teste (os cabeças de chave) jogam contra todos os outros engines, que não jogam entre
  si.
- **H0, H1**: as duas hipóteses de um SPRT: a diferença de Elo é elo0 ou menos (H0), ou elo1 ou mais (H1).
- **Hash**: a memória que um engine ocupa com a tabela de transposição, em MB.
- **LLR**: a razão de log-verossimilhança de um SPRT; o teste termina quando ela sai dos seus limites, calculados a
  partir de alpha e beta.
- **LOS**: likelihood of superiority, a probabilidade de um engine ser mais forte que outro, a partir das partidas
  deles.
- **MultiPV**: um engine que analisa várias linhas ao mesmo tempo.
- **Paralelismo**: o número de partidas jogadas ao mesmo tempo.
- **Pentanomial**: os resultados dos pares de partidas contados pelo total: 0, ½, 1, 1½ ou 2 pontos. Dá barras de
  erro mais estreitas do que contar partidas isoladas, já que as duas partidas de um par compartilham uma abertura.
- **PGN**: o formato de texto das partidas de xadrez: tags como `[White "…"]`, depois os lances.
- **Polyglot**: o formato binário de livros de aberturas (`.bin`) lido pela maioria dos programas, com um peso para
  cada lance.
- **Ponder**: pensar no tempo do adversário; desligado nos torneios do fastchess-gui.
- **SPRT**: o teste sequencial da razão de probabilidades (sequential probability ratio test), que joga partidas até
  poder dizer, com as taxas de erro escolhidas (alpha, beta), se um engine ganha elo1 ou mais (H1) ou não mais que
  elo0 (H0).
- **Suíte de aberturas, livro**: as posições ou os lances a partir dos quais as partidas começam, cada um jogado duas
  vezes com as cores invertidas.
- **Syzygy**: as tablebases de finais que dão o resultado de toda posição com poucas peças (arquivos WDL, `.rtbw`).
- **Tabelas de Berger**: a ordem das rodadas de um todos contra todos em que cada engine joga uma vez por rodada.
- **TF**: time forfeits, as partidas perdidas por tempo.
- **Threads**: as threads de processador com que um engine faz a busca.
- **TLCS, TLCV**: Tom's Live Chess Server e Viewer, um protocolo para acompanhar partidas de engines ao vivo.
- **UCI, WinBoard (CECP)**: os dois protocolos com que os programas conversam com os engines de xadrez.
- **WDL**: as chances de vitória, empate e derrota que um engine dá junto com a pontuação.
