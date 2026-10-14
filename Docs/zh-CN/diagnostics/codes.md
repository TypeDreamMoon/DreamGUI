---
title: ".dui 诊断码"
description: "从原网站迁入的诊断说明，尚待逐节核对当前英文。"
---

<a id="dui1xxx"></a>

# DUI1xxx · 词法

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui1001"></a>

## DUI1001

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不能作为任何记号开头的字符。
```

**触发位置：** `UnexpectedCharacter`

**怎么会出现**：词法器读到一个语法里没有任何记号以它开头的字符：单引号、`$`、`?`、`[`、`]`、反引号，或者落单的 `&`、`|`（只有 `&&`、`||` 是运算符）。连成一串的这种字符只报一次，所以把 Source File 误指向一个根本不是 `.dui` 的文件时，你看到的是一条消息，而不是淹没其他问题的成千上万条。

```dui
Text Title {
    Text = 'Settings'
}
```

**怎么改**：字符串用双引号；如果整个文件都是乱码，检查 Blueprint 的 Source File 指向了哪里。

<a id="dui1002"></a>

## DUI1002

**严重级别：** 错误 (`error`)

**消息：**

```text
一个在行尾之前没有遇到右引号的字符串。
```

**触发位置：** `UnterminatedString`

**怎么会出现**：字符串不能跨行，从左引号到行尾都没有一个未转义的 `"` 就报这个码。常见原因是漏了右引号，或者字符串里的引号没有写成 `\"`。

```dui
Text Title {
    Text = "Settings
}
```

**怎么改**：补上右引号；字符串里的引号写 `\"`，换行写 `\n`。

<a id="dui1003"></a>

## DUI1003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个直到文件末尾都没有遇到 */ 的 /*。
```

**触发位置：** `UnterminatedComment`

**怎么会出现**：块注释从 `/*` 开始，到第一个 `*/` 结束；文件读完了还没遇到，后面的整个文件都被当成了注释。

```dui
/* 设置页
Widget Root {
    Text Title { Text = "Settings" }
}
```

**怎么改**：在注释该结束的地方补上 `*/`；只注释一行时用 `//` 更不容易出这种错。

<a id="dui1004"></a>

## DUI1004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个读不成数字的数字：两个小数点、末尾的点，或者粘了一个单位（24px）。
```

**触发位置：** `MalformedNumber`

**怎么会出现**：以数字开头，却带着数字里不该有的部分：`1.2.3`、`3.`、指数后没有数字的 `1e`、后面什么都没跟的 `-`，或者紧贴着的字母，比如习惯了 CSS 写出来的 `24px`。这些都被吞进同一个记号再报，免得剩下的 `px` 被当成一个节点头，再凭空多出一条不相干的错误。

```dui
Text Title {
    FontSize = 24px
}
```

**怎么改**：写纯数字，比如 `-12`、`0.95`、`1e-45`；单位不写，数值按属性自身的单位读。

<a id="dui1005"></a>

## DUI1005

**严重级别：** 错误 (`error`)

**消息：**

```text
一个十六进制位数不是 3、4、6 或 8 的 # 颜色。
```

**触发位置：** `MalformedHexColor`

**怎么会出现**：颜色写成 `#RGB`、`#RGBA`、`#RRGGBB` 或 `#RRGGBBAA`，按 sRGB 读；其他位数（比如少敲了一位的 `#FF660`），或者 `#` 后面混进了不是十六进制数字的字符，都报这个码。

```dui
Text Note {
    Color = #FF660
}
```

**怎么改**：补齐位数，比如 `#FF6600`；要带透明度就写 8 位，比如 `#FF6600CC`。

<a id="dui1006"></a>

## DUI1006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个比 FName 能容纳的更长的名字。
```

**触发位置：** `IdentifierTooLong`

**怎么会出现**：这门语言写下的每个名字（节点 id、属性名、处理函数名、循环变量）最终都会变成某处的一个 `FName`，而 `FName` 遇到超长的字符串（超过 `NAME_SIZE` 允许的 1023 个字符）不会返回错误，而是直接断言，把编辑器一起带走。所以词法器在记号这一层就拦下它，一条规则覆盖名字能出现的每个位置。没写 id 的节点，如果解析器为它拼出的 id 超长，也报这个码。

**怎么改**：把名字改短；如果超长的是生成的 id，给这个节点或它上面的某个节点写一个 id。

<a id="dui1007"></a>

## DUI1007

**严重级别：** 错误 (`error`)

**消息：**

```text
一个比 FName 能容纳的更长的资产路径。
```

**触发位置：** `AssetPathTooLong`

**怎么会出现**：加载资产时，路径会被拆成包名、对象名这些 `FName`，其中一个超长时引擎同样会停掉编辑器而不是报错（见 DUI1006）。裸写的路径在词法阶段拦下，写成字符串的路径在构建控件树时拦下。

**怎么改**：检查路径是不是拼接出错或者被粘贴了两遍；真实的资产路径不会这么长。

<a id="dui2xxx"></a>

# DUI2xxx · 语法

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui2001"></a>

## DUI2001

**严重级别：** 错误 (`error`)

**消息：**

```text
一个出现在语法期待别的东西的位置上的记号；消息会写出期待什么、遇到了什么。
```

**触发位置：** `UnexpectedToken`

**怎么会出现**：这是解析器的通用错误：期待一个名字却读到别的、`->` 后面没有处理函数名、`for`、`each` 或 `slot` 写在了文件顶层、值后面跟了 `@key` 以外的注解，等等。具体缺了什么，看消息里“期待”的那一半。

```dui
for Item in GetItems() {
    Text Cell { Text <- Item.Name }
}

Widget Root { }
```

**怎么改**：照消息里期待的写法改；上面的例子要把循环放进一个节点里面。

<a id="dui2002"></a>

## DUI2002

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有匹配 } 的 {。
```

**触发位置：** `UnclosedBlock`

**怎么会出现**：节点、`style`、`resources`、`props`、`events`、`timeline`、`rows` 的块都要成对闭合。诊断报在左花括号上，而不是文件末尾，因为“文件在这里结束了”是一个解析器能说出的最没用的真话。

```dui
Widget Root {
    Text Title {
        Text = "Settings"
}
```

**怎么改**：在报出的那个 `{` 该结束的地方补上 `}`；编辑器的折叠和括号高亮可以帮你找到它。

<a id="dui2003"></a>

## DUI2003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有匹配 ) 的 (。
```

**触发位置：** `UnclosedTuple`

**怎么会出现**：元组值（向量、边距、旋转）可以跨行写，所以解析器会一直往下找 `)`；遇到 `}` 或者文件末尾还没找到就报这个码，并停在那里，不会把外层节点剩下的部分也吞进去。绑定表达式里没闭合的括号是 DUI2011。

```dui
Text Label {
    @slot Padding = (0, 4, 0, 4
}
```

**怎么改**：补上 `)`。

<a id="dui2004"></a>

## DUI2004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个既没有 id 也没有块的节点，或者一个带着 (was: …) 却没写 id 的节点。
```

**触发位置：** `MissingNodeId`

**怎么会出现**：没人按名字引用的节点可以不写 id，前提是类型后面跟着块或样式子句（`HorizontalBox { … }`、`Text : Caption`）。一行上只有一个类型名（`Text`）仍然是错误，因为它同样可能是一个丢了 `=` 的属性。`(was: …)` 是改名子句，没有 id 的节点没有“从什么改过来”可言，它需要写出新的 id。

```dui
Widget Root {
    Text
    Text (was: Heading) { Text = "Settings" }
}
```

**怎么改**：给节点写一个 id（`Text Title`）或者一个块（`Text { … }`）；如果本意是属性，补上 `= 值`。

<a id="dui2005"></a>

## DUI2005

**严重级别：** 错误 (`error`)

**消息：**

```text
一个后面没有 = 或箭头、或者 =、箭头后面什么都没有的属性。
```

**触发位置：** `MissingPropertyValue`

**怎么会出现**：属性行写成 `Name = 值`、`Name <- 表达式`、`Name <-> 变量` 或 `Name -> 处理函数`。带点的名字或 `@slot` 行后面直接跟了值，或者 `=` 后面到行尾都没有值，就报这个码。

```dui
Text Title {
    FontSize =
    AnchorData.SizeDelta (400, 240)
}
```

**怎么改**：补上 `=` 和值，或者写成 `<- Function()`。

<a id="dui2006"></a>

## DUI2006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个被编译成类、却没有根节点或者有不止一个根节点的文件。
```

**触发位置：** `MalformedRoot`

**怎么会出现**：编译进 Blueprint 的文件恰好有一个根节点。多出来的根节点在它自己的位置上报出（保留第一个），所以你能看到是哪一个闯了进来。没有根节点的文件是库，只能被 `use`，不能被编译；把一个库设成 Blueprint 的 Source File，就会得到“没有根节点”。

```dui
Widget Root { }
Widget Footer { }
```

**怎么改**：把第二棵树移进第一棵里；如果这个文件本来就是库，别把它当 Source File，在别的文件里 `use` 它。

<a id="dui2007"></a>

## DUI2007

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写了两次、路径为空或者写在节点里的 class。
```

**触发位置：** `MalformedClassDeclaration`

**怎么会出现**：`class /Game/UI/WBP_Settings` 是顶层语句，一个文件最多一行：它说明这个文件是哪个 Blueprint 的源，同时也是文件里所有本地化字符串的命名空间。

```dui
class /Game/UI/WBP_Settings
class /Game/UI/WBP_Options

Widget Root { }
```

**怎么改**：只留一行 `class`，写在文件顶层，后面跟完整的资产路径。

<a id="dui2008"></a>

## DUI2008

**严重级别：** 错误 (`error`)

**消息：**

```text
一个括号里不是恰好一个 id 的 (was: …)。
```

**触发位置：** `MalformedWasClause`

**怎么会出现**：改名子句写成 `(was: OldId)`，冒号后面只能有一个标识符；空着、写了两个词、写成字符串，都报这个码。

```dui
Text Title (was: Old Title) { }
```

**怎么改**：写成 `(was: OldTitle)`，括号里是改名之前的那个 id。

<a id="dui2009"></a>

## DUI2009

**严重级别：** 错误 (`error`)

**消息：**

```text
一个括号里不是恰好一个字符串、或者跟在非字符串值后面的 @key(…)。
```

**触发位置：** `MalformedKeyOverride`

**怎么会出现**：`@key("Dialog.Confirm")` 给一个字符串指定它的本地化键。括号里要恰好一个非空的带引号字符串；它也只能跟在带引号的字符串后面，因为只有字符串会被本地化，跟在数字后面是一个看起来生效、实际什么也不做的设置。

```dui
Text Confirm {
    Text = "OK" @key(Dialog.Confirm)
    FontSize = 14 @key("Dialog.Size")
}
```

**怎么改**：写成 `Text = "OK" @key("Dialog.Confirm")`；不是字符串的值，去掉 `@key`。

<a id="dui2010"></a>

## DUI2010

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写法不是 <keyword> Var in Source 或 Source() 的 for / each 头部。
```

**触发位置：** `MalformedLoopHeader`

**怎么会出现**：循环头依次是关键字、循环变量、`in`、来源。来源是一个变量名（`Options`）或者一个无参函数调用（`GetOptions()`）；漏了 `in`、来源不是一个名字、调用里写了实参，都报这个码。

```dui
VerticalBox Options {
    for Option GetOptions() {
        Text { Text <- Option.Label }
    }
}
```

**怎么改**：写成 `for Option in GetOptions()`；需要参数的来源，改成一个无参函数，在函数里面去传参。

<a id="dui2011"></a>

## DUI2011

**严重级别：** 错误 (`error`)

**消息：**

```text
一个解析不成表达式的 <-、<-> 右边或者 emit 实参。
```

**触发位置：** `MalformedBindingExpression`

**怎么会出现**：右边应当是函数调用、变量、字面量或它们用运算符组合起来的式子。多出来的记号、没闭合的括号或调用、`@` 后面没有资源名、`.` 后面没有成员名、`<->` 后面不是一个变量名，都报这个码。注意没有 `/`（它属于路径和注释），也没有 `? :`。

```dui
Widget Panel {
    RenderOpacity <- Fade() *
    Shown <- (HasDetail()
}
```

**怎么改**：把表达式补完整；除法和条件选择写进一个函数，再绑定那个函数。

<a id="dui2012"></a>

## DUI2012

**严重级别：** 错误 (`error`)

**消息：**

```text
一个无法兑现的 use：文件不存在、读不出来、解析不过，或者导入形成了环。
```

**触发位置：** `ImportFailed`

**怎么会出现**：`use` 后面要么是带引号的路径，要么是类路径加 `as 名字`。路径在所有 `DUI/` 根下都找不到可读的文件、被导入的文件自己有解析错误、文件导入了自己、导入链绕回到一个正在导入的文件，都报在这一行 `use` 上。

```dui
use "UI/Libary.dui"

Widget Root { }
```

**怎么改**：检查路径的拼写和它相对的是哪个 `DUI/` 根；被导入的文件有错误时，直接编译它，看具体是什么错；有环就把共享的部分拆到第三个文件里。

<a id="dui2013"></a>

## DUI2013

**严重级别：** 错误 (`error`)

**消息：**

```text
一个嵌套深度超过 256 层的块或括号。
```

**触发位置：** `NestingTooDeep`

**怎么会出现**：递归下降的解析器碰上嵌套上千层的文件会耗尽栈，而栈溢出不是诊断，是编辑器带着作者未保存的工作一起消失。256 层远高于手写或生成的 `.dui` 会达到的深度，碰到它说明文件是坏的或者别有用心，而不仅仅是深；这一层以下的内容不会被读取。

**怎么改**：检查生成这个文件的工具；真实的界面应该把深层的结构拆成组件。

<a id="dui2014"></a>

## DUI2014

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不合语法的 timeline 块或其中的一行。
```

**触发位置：** `MalformedTimeline`

**怎么会出现**：整个块共用一个码，消息会说明是哪一部分错了：头部没有名字；`external` 后面又跟了块，或者既没有块也没有 `external`；`duration`、`loop` 没有值，或者 `loop` 不是 `Once`、`Loop`、`PingPong`；轨道行缺 `:`，或者只写了节点路径没写属性；关键帧缺 `=`；时间是负数；`ease` 后面没有曲线名；`@时间` 后面没有 `->` 和事件名。

```dui
timeline Pulse {
    duration = 0.6
    loop     = Forever
    Icon.RenderScale 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1)
}

Widget Root {
    Image Icon
}
```

**怎么改**：照文档的形状写：轨道行是 `节点路径.属性 : 时间 = 值 ease 曲线名, …`，事件键是 `@0.3 -> Landed`。

<a id="dui2015"></a>

## DUI2015

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 as 后面没有名字、名字是关键字，或者类路径没写 as 的 use。
```

**触发位置：** `MalformedUseDeclaration`

**怎么会出现**：`as` 后面是一个词，这个词就是完整的名字，后面不能再跟东西，保留关键字也不能拿来当名字。`use /Game/UI/WBP_Row` 这样的类路径除了类本身没有别的可带来，没有名字的话这一行什么也不做，所以必须写 `as`。

```dui
use "UI/Components/Row.dui" as
use /Game/UI/WBP_Slider

Widget Root { }
```

**怎么改**：写成 `use "UI/Components/Row.dui" as Row`、`use /Game/UI/WBP_Slider as Slider`。

<a id="dui2016"></a>

## DUI2016

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不是 Type Name 或 Type Name = value 的 props 行，或者写在了节点里的 props。
```

**触发位置：** `MalformedPropsBlock`

**怎么会出现**：`props` 块每行声明一个属性：类型、名字，以及可选的 `= 默认值`，`Enum` 类型在名字前先写枚举的路径。缺名字、`=` 后面没有值、一行写了两个名字，都报这个码；`props` 声明的是这个文件的类有什么，所以它属于文件顶层，写进节点里也报这个码。

```dui
props {
    Text
    Number Count =
}

Widget Root { }
```

**怎么改**：写成 `Text Label`、`Number Count = 0`，并把 `props` 块放到文件顶层。

<a id="dui2017"></a>

## DUI2017

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不是 Name 或 Name(Type Param, …) 的 events 条目，或者写在了节点里的 events。
```

**触发位置：** `MalformedEventsBlock`

**怎么会出现**：每个事件是一个名字，可以跟一个括号括起的参数列表，每个参数是类型加名字，用 `,` 分隔、`)` 结束；条目在换行或 `;` 处结束。参数缺名字、分隔符不对、条目后面还跟着别的东西，或者 `events` 写进了节点，都报这个码。

```dui
events {
    Picked(Number)
}

Widget Root { }
```

**怎么改**：写成 `Picked(Number Index)`，并把 `events` 块放到文件顶层。

<a id="dui2018"></a>

## DUI2018

**严重级别：** 错误 (`error`)

**消息：**

```text
一个缺了条件或块的 if、前面没有 if 的 else，或者一个装了控件以外东西的分支。
```

**触发位置：** `MalformedConditional`

**怎么会出现**：`if` 写在节点里面，后面是一个绑定表达式作条件，再跟 `{ … }`；`else` 后面跟块，或者跟 `if` 和新的条件。分支里只能放控件：属性、`@slot` 行、`+` 块、`slot`、循环放进去都报这个码，`if` 写在文件顶层也是。分支里的控件也不能写 `Shown <->`，因为 `Shown` 由分支决定，镜像会把它写回变量。

```dui
Widget Root {
    if HasSave() {
        RenderOpacity = 0.5
        Text Continue { Text = "Continue" }
    }
}
```

**怎么改**：把属性写到分支里的某个控件上；补全条件或块；`else` 写在 `if` 的 `}` 同一行或下一行。

<a id="dui2019"></a>

## DUI2019

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写了两次 default 的 slot，或者一个既声明又填充的 slot 块。
```

**触发位置：** `MalformedSlotDeclaration`

**怎么会出现**：组件文件里的 `slot Name { … }` 是声明，块里放组件、属性和 `@slot` 行，用来布局这个洞；宿主里的 `slot Name { … }` 是填充，块里只放控件。一个块里两样都有，或者填充上写了 `default`、样式、`(was: …)`，又或者填充块里写了属性，都报这个码。

```dui
Widget Root {
    slot Footer default default
    slot Detail {
        + VerticalBox { Spacing = 15 }
        Text Note { Text = "A length of wire." }
    }
}
```

**怎么改**：声明里不放控件，填充里只放控件；`default`、样式和改名都写在声明上。

<a id="dui2020"></a>

## DUI2020

**严重级别：** 错误 (`error`)

**消息：**

```text
一个读不通的 rows 表：列清单不是属性名、某一列写了两次，或者某一行的值个数不对。
```

**触发位置：** `MalformedRows`

**怎么会出现**：表头是 `rows 类型 : 样式 (列, 列)`，列是属性名，每个只写一次（不区分大小写，因为大小写不同的两列指的是同一个属性）；每一行的值和列一一对应，用逗号分隔。少一个值说明作者漏写了什么，多一个值说明有一个值没处可去，两种都只能靠猜，所以读不通的行不产生控件。

```dui
VerticalBox Menu {
    rows Text (Text, FontSize) {
        "City Ruins", 18
        "Desert Zone"
    }
}
```

**怎么改**：让每一行的值数等于列数；某一行需要列以外的东西时，在这一行末尾加一个块。

<a id="dui2021"></a>

## DUI2021

**严重级别：** 错误 (`error`)

**消息：**

```text
一行读不通的 viewmodels，或者一个写在节点里的 viewmodels 块。
```

**触发位置：** `MalformedViewModelsBlock`

**怎么会出现**：`viewmodels` 块里每行声明一个视图模型：`类型 名字`，后面可以跟 `= new`、`= global`、`= global "Name"`、`= parent` 或 `= parent "Name"`。缺名字、`=` 后面不是这几种来源之一、一行里写了第二个声明，都是这一条。`global ""` 这样的空名字也是：它什么都不指，和不写名字的"只按类找"不是一回事。块写在节点里面同样是这一条：视图模型是类持有的东西，和 `props`、`events` 一样只能写在文件顶层。

```dui
viewmodels {
    PlayerVM Player = global ""
    SettingsVM = new
}
```

**怎么改**：每行写成 `类型 名字`，再加可选的来源；只想按类从注册表里取，就只写 `global`；把块挪到文件顶层。

<a id="dui3xxx"></a>

# DUI3xxx · 语义

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui3001"></a>

## DUI3001

**严重级别：** 错误 (`error`)

**消息：**

```text
两个用了同一个 id 的节点。
```

**触发位置：** `DuplicateNodeId`

**怎么会出现**：id 是节点的身份：它同时是 guid、类成员变量名、绑定的键和本地化键。比较不区分大小写，因为它最终变成一个 `FName`。重复的 id 永远不会被悄悄改成 `OkBtn_1`，那样会让你的绑定悄悄指向另一个节点。slot 和控件共用这个命名空间，所以两个同名的 slot、一个和控件同名的 slot 也是这个码；宿主里填充组件 slot 的 `slot Detail { … }` 不算。

```dui
Widget Root {
    Text Title { Text = "Settings" }
    Text title { Text = "Audio" }
}
```

**怎么改**：给其中一个改名；如果冲突来自一次改名，用 `(was: …)` 让下一次编译把引用迁移过去。

<a id="dui3002"></a>

## DUI3002

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不是标识符、以数字开头或者是关键字的 id。
```

**触发位置：** `InvalidNodeId`

**怎么会出现**：id 要能当 C++ 标识符：字母、数字和 `_`，不以数字开头（ASCII 以外的字符算字母，所以 id 可以是中文）。`2ndPanel` 会被拒绝，因为生成的类会把它声明成 `_2ndPanel`，照着文件里的名字写的绑定就全都对不上。`class`、`style`、`resources`、`slot`、`for`、`each`、`in`、`was`、`use`、`timeline`、`external`、`ease` 是关键字，不能当节点 id 或 slot 名；超过 `FName` 长度的 id 也报这个码。

```dui
Widget Root {
    Text 2ndLine { }
    Text external { }
}
```

**怎么改**：换一个以字母或 `_` 开头、不是关键字的 id，比如 `SecondLine`。

<a id="dui3003"></a>

## DUI3003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个既不是标签、容器、别名、注册控件，也不是路径的节点类型。
```

**触发位置：** `UnknownNodeType`

**怎么会出现**：节点类型按顺序解析：内置标签（`Text`、`Image` 等）、布局容器（`VerticalBox` 等）、`use … as` 给出的组件别名、注册标签（`Native.Button` 等）、资产路径（以 `/` 开头）。都对不上就报这个码；对带作用域或命名空间的名字，消息会列出那里实际有哪些。

```dui
Widget Root {
    Txt Title { Text = "Settings" }
}
```

**怎么改**：修正拼写；组件先用 `use "…" as Name` 引进来，或者写它的完整资产路径。

<a id="dui3004"></a>

## DUI3004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个指向这个文件看不到的样式的 : Name。
```

**触发位置：** `UnknownStyle`

**怎么会出现**：节点的样式子句或者样式的基类，用了一个既没有在本文件声明、也没有被 `use` 带进来的样式名。样式可以写在戴它的节点下面，先后不影响。带命名空间的名字（`nier.Label`）如果命名空间本身没被声明，报的是 DUI3021 而不是这个码。

```dui
style Caption { FontSize = 14 }
Text Note : Captoin { Text = "Ready" }
```

**怎么改**：修正样式名，或者 `use` 声明了它的那个库。

<a id="dui3005"></a>

## DUI3005

**严重级别：** 错误 (`error`)

**消息：**

```text
同一个文件里的两个同名样式。
```

**触发位置：** `DuplicateStyle`

**怎么会出现**：本地样式可以遮蔽导入的同名样式，但两个本地样式同名时，构建器和写回器可能对“一个节点到底拿到了哪些属性”得出不同的答案，所以第二个被拒绝。

```dui
style Caption { FontSize = 14 }
style Caption { FontSize = 12 }

Text Note : Caption { Text = "Ready" }
```

**怎么改**：合并成一个，或者改名其中一个；要在一个样式上做变体，用继承，比如 `style Small : Caption`。

<a id="dui3006"></a>

## DUI3006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个指向控件无法携带的东西的 + Name。
```

**触发位置：** `UnknownBehaviourClass`

**怎么会出现**：`+` 后面的类名会依次加上插件的前缀去找（`UIButton` 和 `Button` 都能找到按钮行为），也可以写 `/Script/Module.Class` 全路径；找不到一个行为、布局容器或 layout-self 对象时报这个码。样式里的 `+` 行也一样。

```dui
Widget Confirm {
    + UIButon { TransitionType = None }
}
```

**怎么改**：修正类名，或者写出完整的 `/Script/…` 路径。

<a id="dui3007"></a>

## DUI3007

**严重级别：** 错误 (`error`)

**消息：**

```text
这个码已经退役、从不报出，两个同名 slot 现在报 DUI3001。
```

**触发位置：** `DuplicateSlotName_Retired`

<a id="dui3008"></a>

## DUI3008

**严重级别：** 警告 (`warning`)

**消息：**

```text
一个遮蔽了外层循环变量的循环变量（警告）。
```

**触发位置：** `ShadowedLoopVariable`

**怎么会出现**：嵌套的 `for` / `each` 用了和外层一样的变量名。它只是警告，因为遮蔽本身改变不了构建出来的树。不过循环本来就不能嵌套：构建时内层循环会被 DUI5021 或 DUI5012 拒绝。

```dui
VerticalBox Groups {
    for Item in GetGroups() {
        VerticalBox Group {
            for Item in GetEntries() {
                Text Entry { Text <- Item.Name }
            }
        }
    }
}
```

**怎么改**：内层换一个变量名；更要紧的是把内层循环放进一个组件，由组件自己的文件去循环，列表经由它的 `props` 传进去。

<a id="dui3009"></a>

## DUI3009

**严重级别：** 错误 (`error`)

**消息：**

```text
一个不肯接受这个子节点的父节点：已经满了的内容控件，或者被填了两次的命名 slot。
```

**触发位置：** `ParentRefusedChild`

**怎么会出现**：节点没问题，父节点也没问题，只是这一对不行。`SizeBox`、`ScaleBox`、`SafeZone`、`Border` 只放一个子节点；命名 slot 只放一个控件，同一个 slot 也不能填两次；嵌套成环同样被拒绝。挂接时多出来的子节点会被悄悄丢掉，没有这个码，你会得到一棵少了一截的树，而那一截明明就写在你的文件里。

```dui
SizeBox Box {
    Text A { Text = "One" }
    Text B { Text = "Two" }
}
```

**怎么改**：把几个子节点放进一个容器（比如 `VerticalBox { … }`）再放进去；需要一个 slot 放多个控件时，在声明里给它一个布局容器。

<a id="dui3010"></a>

## DUI3010

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 (was: X)，而 X 仍然是这个文件里某个节点的 id。
```

**触发位置：** `RenameOldIdStillInUse`

**怎么会出现**：你把节点从 X 改了名，又新建了一个叫 X 的节点。“把指向 X 的东西都搬过来”会把引用从一个确实存在、也需要它们的节点上搬走，所以拒绝；而且整个文件的迁移都不做，因为迁移了一半的文件比没迁移的更糟：下一次编译就不再从作者写下的样子出发了。

```dui
Widget Root {
    Text Title (was: Heading) { Text = "Settings" }
    Text Heading { Text = "Audio" }
}
```

**怎么改**：先给另一个节点改名，或者删掉 `(was: Heading)`。

<a id="dui3011"></a>

## DUI3011

**严重级别：** 错误 (`error`)

**消息：**

```text
两个都声称 (was: X) 的节点。
```

**触发位置：** `DuplicateWasId`

**怎么会出现**：一个旧名字不能变成两个新名字，没有办法决定由谁继承 X 的引用，所以整个文件的迁移都不做。

```dui
Widget Root {
    Text Title (was: Heading) { }
    Text Subtitle (was: Heading) { }
}
```

**怎么改**：把子句留在要继承引用的那个节点上，删掉另一个。

<a id="dui3012"></a>

## DUI3012

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写在节点 X 自己身上的 (was: X)。
```

**触发位置：** `SelfRename`

**怎么会出现**：改名子句说的是节点以前叫什么，名字没变就没有可迁移的东西，几乎一定是笔误；整个文件的迁移都不做。

```dui
Text Title (was: Title) { Text = "Settings" }
```

**怎么改**：删掉这个 `(was: …)`；如果是想改名，把新的 id 写在类型后面，旧的写进 `(was: …)`。

<a id="dui3013"></a>

## DUI3013

**严重级别：** 警告 (`warning`)

**消息：**

```text
一个同时是别的成员名字的旧 id，因此图表引用没有被迁移（警告）。
```

**触发位置：** `RenameGraphReferenceAmbiguous`

**怎么会出现**：Blueprint 图表里的引用按名字解析。如果旧名字同时是这个 Blueprint 自己声明的变量、父类的成员，或者某个函数里的局部变量，改写所有对它的引用就会悄悄改动与这个控件毫无关系的代码，所以图表这一路不做。绑定和动画轨道这两路没有歧义，照常迁移。

**怎么改**：手动把图表里的引用指向新名字，或者给另一个同名的东西改名。

<a id="dui3014"></a>

## DUI3014

**严重级别：** 错误 (`error`)

**消息：**

```text
两个同名的资源条目。
```

**触发位置：** `DuplicateResource`

**怎么会出现**：条目会在多个 `resources` 块之间累积；一个名字声明了两次时，第一个处处生效，所以第二个被拒绝。

```dui
resources {
    Color Accent = #FF6600
    Color Accent = #E05A47
}

Text Note { Color = @Accent }
```

**怎么改**：删掉其中一个，或者给它改名。

<a id="dui3015"></a>

## DUI3015

**严重级别：** 错误 (`error`)

**消息：**

```text
一个沿着基类链继承了自己的样式。
```

**触发位置：** `StyleCycle`

**怎么会出现**：`style A : B`，沿着基类往上走又回到了 A。这时什么都不应用，戴着这个样式的节点报这个码。

```dui
style Caption : Warning { FontSize = 14 }
style Warning : Caption { Color = #E05A47 }

Text Note : Warning { Text = "Unsaved changes" }
```

**怎么改**：断开这个环，让其中一个不再继承另一个。

<a id="dui3016"></a>

## DUI3016

**严重级别：** 错误 (`error`)

**消息：**

```text
两个同名的时间轴。
```

**触发位置：** `DuplicateTimeline`

**怎么会出现**：时间轴的名字既是动画的显示名，也是一个类成员变量（编译器为每个动画声明一个，和 UMG 为 `UWidgetAnimation` 做的一样），两个同名就是一个谁也访问不到的变量，和两个同名样式一样被拒绝。

```dui
timeline Pulse external
timeline Pulse { duration = 0.6 }

Widget Root { }
```

**怎么改**：给其中一个改名。

<a id="dui3017"></a>

## DUI3017

**严重级别：** 错误 (`error`)

**消息：**

```text
两行给出同一个名字的 use … as。
```

**触发位置：** `DuplicateComponentAlias`

**怎么会出现**：第一行处处生效，第二行被拒绝，免得一个类型名因为远处的某一行而悄悄换了意思。

```dui
use "UI/Components/Row.dui" as Row
use "UI/Components/NieR_Row.dui" as Row

Widget Root { }
```

**怎么改**：给第二个换一个名字。

<a id="dui3018"></a>

## DUI3018

**严重级别：** 错误 (`error`)

**消息：**

```text
一个与标签或容器同名的 use … as 名字。
```

**触发位置：** `AliasShadowsBuiltIn`

**怎么会出现**：节点类型先问内置标签和布局容器，再问别名，所以叫 `Text` 或 `VerticalBox` 的别名永远写不出来：别名改变不了 `Text` 的意思。

```dui
use "UI/Components/Label.dui" as Text

Widget Root { }
```

**怎么改**：换一个不是标签或容器名的名字，比如 `Label`。

<a id="dui3019"></a>

## DUI3019

**严重级别：** 错误 (`error`)

**消息：**

```text
两行声明同一个名字的 props。
```

**触发位置：** `DuplicateProp`

**怎么会出现**：一个名字只能是一个属性；第一行保留，第二行被拒绝。

```dui
props {
    Text   Label
    String Label
}

Widget Root { }
```

**怎么改**：删掉其中一行，或者给它改名。

<a id="dui3020"></a>

## DUI3020

**严重级别：** 错误 (`error`)

**消息：**

```text
两个同名的 events 条目，或者一个条目里声明了两次的参数。
```

**触发位置：** `DuplicateEvent`

**怎么会出现**：每个事件都会变成一个事件分发器，所以名字必须唯一；同一个事件的参数名也必须唯一。

```dui
events {
    Picked(Number Index, Number Index)
    Closed
    Closed
}

Widget Root { }
```

**怎么改**：删掉重复的那个，或者给它改名。

<a id="dui3021"></a>

## DUI3021

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 ns 没有被任何 use … as ns 声明过的 ns.Name。
```

**触发位置：** `UnknownNamespace`

**怎么会出现**：样式、资源和组件别名可以带命名空间前缀（`: nier.Label`、`@nier.Ink`、`nier.Row`），这个前缀必须来自 `use "…" as nier`。这种情况只报这一个码，不会再报一条“没有这个样式”，把你引去找一个并不存在的拼写错误。

```dui
Text Note : nier.Label { Text = "Ready" }
```

**怎么改**：加上 `use "UI/NieR_Common.dui" as nier`，或者修正前缀的拼写。

<a id="dui3022"></a>

## DUI3022

**严重级别：** 错误 (`error`)

**消息：**

```text
一个文件里不止一个 slot … default。
```

**触发位置：** `MultipleDefaultSlots`

**怎么会出现**：嵌套进来却没写 slot 名的内容只有一个去处。第一个保留默认的角色，这样已经按它编译过的宿主仍然一致。

```dui
Widget Root {
    slot Rows default
    slot Footer default
}
```

**怎么改**：只在一个 slot 上写 `default`。

<a id="dui3023"></a>

## DUI3023

**严重级别：** 警告 (`warning`)

**消息：**

```text
一个 rows 表里第一个值生成了同一个 id 的两行（警告）。
```

**触发位置：** `DuplicateRowKey`

**怎么会出现**：每一行的 id 来自它的第一个值（比如 `Page_0__Row_City_Ruins`），这样插入或调整行的顺序不会动到别的行的 id 和本地化键。两行的键生成了同一个 id，或者生成了文件里已经有的 id 时，后一行会在后面加上 `_1` 这样的序号，而这个序号连同它的本地化键会随着行的顺序移动，这恰恰是键列要避免的不稳定。

```dui
VerticalBox Menu {
    rows Text (Text) {
        "Audio"
        "Audio"
    }
}
```

**怎么改**：让各行的第一个值互不相同；需要按名字找到的行，写成单独的节点。

<a id="dui3024"></a>

## DUI3024

**严重级别：** 错误 (`error`)

**消息：**

```text
两个同名的 viewmodels 条目。
```

**触发位置：** `DuplicateViewModel`

**怎么会出现**：每个条目会成为类上的一个变量，两个同名条目只能有一个变量。名字的比较不区分大小写，`Player` 和 `player` 是同一个变量。保留第一个。

```dui
viewmodels {
    PlayerVM Player
    PlayerVM player = global
}
```

**怎么改**：给其中一个改名，或者删掉多余的那一行。

<a id="dui4xxx"></a>

# DUI4xxx · 取值

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui4001"></a>

## DUI4001

**严重级别：** 错误 (`error`)

**消息：**

```text
目标对象上没有这个名字的属性；消息会给出最接近的那个。
```

**触发位置：** `UnknownProperty`

**怎么会出现**：裸的属性名依次在控件、它的视觉对象、（容器类型的节点上）它的布局容器、它的行为上查找，第一个有它的接收这个值，都没有就报这个码。`Width`、`Height`、`AnchorLeft` 这类细节面板上显示的标签不是属性名，消息会直接告诉你该怎么写。

```dui
Text Title {
    FontSzie = 14
    Width = 400
}
```

**怎么改**：照建议改名；尺寸写 `AnchorData.SizeDelta = (w, h)`，锚点写 `AnchorData.AnchorMin` 和 `AnchorData.AnchorMax`。

<a id="dui4002"></a>

## DUI4002

**严重级别：** 错误 (`error`)

**消息：**

```text
一条开头能解析、后面解析不了的带点路径。
```

**触发位置：** `UnknownPropertyPathSegment`

**怎么会出现**：`A.B` 里 `A` 是一个属性，但它不是结构体，没有字段可以往下走；或者它是结构体，却没有叫 `B` 的字段，这时消息会给出最接近的字段名。

```dui
Widget Panel {
    AnchorData.Size = (400, 240)
}
```

**怎么改**：照建议修正字段名，比如 `AnchorData.SizeDelta`。

<a id="dui4003"></a>

## DUI4003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个形状不可能成为该属性类型的值。
```

**触发位置：** `ValueTypeMismatch`

**怎么会出现**：字面量的种类与属性的类型对不上，比如给数字属性写字符串、给 `FText` 写没加引号的词；资产路径加载出来的不是属性要的类；当作值写的节点 id 指向的节点缺少属性要的那一部分（比如没有视觉对象），或者是另一种控件类；`FName` 属性的字符串超长；时间轴的关键帧值读不成那个属性的类型。

```dui
Text Title {
    FontSize = "large"
    Text = Settings
}
```

**怎么改**：按属性的类型写值：文本一律加引号，对象属性写能加载出正确类的路径。

<a id="dui4004"></a>

## DUI4004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个元素个数不对的元组。
```

**触发位置：** `TupleArityMismatch`

**怎么会出现**：元组按目标结构体读：二维向量 2 个，三维向量和旋转 3 个，边距 4 个。

```dui
Widget Panel {
    AnchorData.SizeDelta = (400, 240, 0)
}
```

**怎么改**：按结构体的分量个数写，比如 `(400, 240)`、`(0, 8, 0, 0)`。

<a id="dui4005"></a>

## DUI4005

**严重级别：** 错误 (`error`)

**消息：**

```text
一个属性的枚举没有声明的词。
```

**触发位置：** `UnknownEnumValue`

**怎么会出现**：枚举属性上写的词不是这个枚举的值（消息会给出最接近的那个），或者写的数字不对应它声明的任何值。标志位枚举没有 `|`，组合要写成各标志相加后的数字。

```dui
Widget Panel {
    Visibility = Invisible
}
```

**怎么改**：照建议改成枚举里的值，比如 `Collapsed` 或 `Hidden`。

<a id="dui4006"></a>

## DUI4006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个文本写不了的属性：瞬态的、已弃用的、属于对象图的，或者是委托。
```

**触发位置：** `PropertyNotWritable`

**怎么会出现**：属性存在，但从 `.dui` 写它没有意义：瞬态属性（没有原生 setter 的那种）写进去的值保存不下来；已弃用的属性不再接受新值；`Visual`、`LayoutContainer` 这类实例化引用本身就是对象图，由嵌套和 `+` 来创作，而不是赋值；委托没有文本形式，事件要用 `->` 路由。

```dui
Widget Panel {
    LayoutContainer = None
}
```

**怎么改**：容器用 `+ VerticalBox { … }` 或容器类型的节点来写，事件用 `->`；其余情况换一个可以写的属性。

<a id="dui4007"></a>

## DUI4007

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有指向任何资源条目的 @Name。
```

**触发位置：** `UnknownResource`

**怎么会出现**：`@Accent` 代表 `resources` 块里叫 `Accent` 的那个条目（本文件的，或 `use` 带进来的）；找不到就报这个码。用 `@Name` 写节点类型时也一样，它要求一个 `Asset` 条目。

```dui
resources {
    Color Accent = #FF6600
}

Text Note {
    Color = @Accnet
}
```

**怎么改**：修正名字，或者在 `resources` 块里声明这个条目。

<a id="dui4008"></a>

## DUI4008

**严重级别：** 错误 (`error`)

**消息：**

```text
一个值与声明的类型不符的资源，或者一个被当作节点类型、却不是 Asset 的资源。
```

**触发位置：** `ResourceTypeMismatch`

**怎么会出现**：资源的类型是 `Color`、`Number`、`Vector2`、`String`、`Asset` 之一，值要符合它声明的类型；类型名不是这五个之一也报这个码。用 `@Name` 当节点类型时，那个条目必须是指向控件类的 `Asset`。

```dui
resources {
    Color Accent = 8
}

Text Note { Color = @Accent }
```

**怎么改**：让值符合类型（`Color Accent = #FF6600`），或者改正声明的类型。

<a id="dui5xxx"></a>

# DUI5xxx · 构建

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui5001"></a>

## DUI5001

**严重级别：** 错误 (`error`)

**消息：**

```text
一个加载不出来的资产或类路径。
```

**触发位置：** `AssetNotFound`

**怎么会出现**：节点类型或属性值写的资产路径加载失败：路径拼错、资产被移动或删除，或者它所在的插件没有启用。

```dui
Widget Root {
    /Game/UI/WBP_Rwo Audio { }
}
```

**怎么改**：在内容浏览器里复制资产的路径重新写上；资产挪过位置的话，把路径一起更新。

<a id="dui5002"></a>

## DUI5002

**严重级别：** 错误 (`error`)

**消息：**

```text
一个属于视觉对象、而节点的类型并不创建那个视觉对象的属性。
```

**触发位置：** `NoVisualForProperty`

**怎么会出现**：属性名在控件上找不到，却是某个标签的视觉对象的属性，比如在 `Widget` 上写 `FontSize`，而它属于 `Text` 节点创建的文本视觉对象。消息会说出哪个标签有它，通常改一个词就好。

```dui
Widget Title {
    FontSize = 14
}
```

**怎么改**：把节点类型改成消息里点名的那个标签（这里是 `Text Title`），或者把这一行移到那样的节点上。

<a id="dui5003"></a>

## DUI5003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个父节点不布局面板、却写了 slot 行的节点。
```

**触发位置：** `NoPanelSlotForProperty`

**怎么会出现**：`@slot`、`@fill` 设置的是面板 slot，也就是父节点的容器怎样摆放这个子节点。父节点没有布局容器（普通的 `Widget`），或者节点就是根，就没有面板 slot 可写；样式带来的 slot 行也算。

```dui
Widget Root {
    Text Label {
        @slot Padding = (0, 4, 0, 4)
    }
}
```

**怎么改**：给父节点一个布局容器（写成 `VerticalBox Root` 或加 `+ VerticalBox { }`），或者删掉这些 slot 行，改用 `AnchorData` 定位。

<a id="dui5004"></a>

## DUI5004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个类里没有的绑定函数，或者一个带参数的绑定函数。
```

**触发位置：** `BindingFunctionNotFound`

**怎么会出现**：`Text <- GetTitle()` 这样只调一个函数的绑定会直接绑定那个函数，它必须是这个 Blueprint（或它的父类）上的函数，不带参数，返回值是被绑定属性的类型。函数名超出 `FName` 长度也报这个码。

```dui
Text Title {
    Text <- GetTitel()
}
```

**怎么改**：修正函数名，或者在 Blueprint 里加上这个函数；需要参数时，写一个无参函数在里面传参。

<a id="dui5005"></a>

## DUI5005

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有 setter 的被绑定属性。
```

**触发位置：** `BindingTargetHasNoSetter`

**怎么会出现**：绑定在运行时通过属性的 setter 写入：名字是 `Set` 加属性名（布尔属性去掉前缀 `b`，`bUseKerning` 对应 `SetUseKerning`），只有一个参数、没有返回值。找不到这样的函数，这个属性就驱动不了。组件自己的 `props` 是唯一的例外：它们是 Blueprint 变量，没有 setter，但可以用 `<-` 单向绑定。

**怎么改**：改为在一个函数里设置它；属性是你自己的 C++ 类上的话，给它加一个符合这个形状的 setter。

<a id="dui5006"></a>

## DUI5006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个以不是具体 DreamUI 用户控件的类作类型的节点。
```

**触发位置：** `NotAUserWidgetClass`

**怎么会出现**：以类作类型的节点（别名、`@Name`、注册标签、资产路径）是一个组件实例，它的内容来自那个类，所以类必须是 `UDreamUserWidget` 的子类，而且不能是抽象的或已弃用的。指向贴图、普通 UMG 控件或其他资产都会报这个码。

```dui
Widget Root {
    /Game/UI/T_Icon Icon { }
}
```

**怎么改**：资产当作值写到属性上（比如 `Image` 的 `Brush.ResourceObject`）；节点类型只用 DreamGUI 控件 Blueprint 或原生的 DreamUI 用户控件类。

<a id="dui5007"></a>

## DUI5007

**严重级别：** 警告 (`warning`)

**消息：**

```text
一个调用方没有地方记录、因而被跳过的循环（警告）。
```

**触发位置：** `LoopNotExpanded`

**怎么会出现**：`for` 和 `each` 会被记录成编译后的循环绑定，构建控件树时需要调用方提供记录它们的地方。编译 Blueprint 时总会提供；直接拿 AST 构建树的调用方（多数测试、自己写的 C++ 工具）可能没有提供，这时循环和它下面的一切被跳过，文件其余部分照常构建。

**怎么改**：通过 Blueprint 编译这个文件；自己调用构建器时，给它一个记录循环绑定的地方。

<a id="dui5008"></a>

## DUI5008

**严重级别：** 错误 (`error`)

**消息：**

```text
一个落在任何绑定都无法指名的东西上的绑定：面板 slot、布局容器，或者结构体里的字段。
```

**触发位置：** `BindingTargetNotSupported`

**怎么会出现**：绑定能指名的目标只有控件本身、它的视觉对象和它的行为（`EDreamWidgetBindingTarget`），而且只能是完整的属性。`@slot` 属性、容器类型节点上属于容器的属性、`AnchorData.SizeDelta` 这样的结构体字段，不管有没有 setter 都没有地方可记录。和 DUI5005 分开，正是因为补一个 setter 在这里没用。

```dui
VerticalBox Column {
    Spacing <- GetGap()
    AnchorData.SizeDelta <- GetSize()
}
```

**怎么改**：绑定整个属性（比如一个 `FVector2D` 属性），或者在 Blueprint 的函数里设置这些值。

<a id="dui5009"></a>

## DUI5009

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有根节点、因而无从构建的树。
```

**触发位置：** `NothingToBuild`

**怎么会出现**：交给构建器的 AST 里没有根节点。原因解析器已经报过了（通常是 DUI2006），这一条只说明结果。

**怎么改**：先处理同一次编译里更早的那条诊断。

<a id="dui5010"></a>

## DUI5010

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 X -> …，而 X 不是目标上的事件。
```

**触发位置：** `EventNotFound`

**怎么会出现**：`->` 左边要是控件、它的视觉对象或行为上的事件：一个 `BlueprintAssignable` 动态多播委托，或者一个可编辑的 `DreamUIEventDelegate`。名字指向的是普通属性、指向结构体里的某个路径，或者在这些对象上都找不到，就报这个码；处理函数名超出 `FName` 长度也是。

```dui
Text Title {
    OnClick -> HandleClick
}
```

**怎么改**：把路由写到拥有这个事件的地方，比如按钮行为的块里：`+ UIButton { OnClick -> HandleClick }`。

<a id="dui5011"></a>

## DUI5011

**严重级别：** 错误 (`error`)

**消息：**

```text
一个编译器无法转成函数的表达式。
```

**触发位置：** `BindingExpressionUnsupported`

**怎么会出现**：比单纯调用一个函数更复杂的 `<-` 表达式会被编译成一个生成的 Blueprint 函数。名字既不是这个类的变量也不是函数（按上一次编译后的类查找）、实参个数不对、函数没有返回值、运算符在这两种操作数类型之间没有定义、类型之间没有转换、在表达式里用了不是 `Number` 或 `String` 的资源，或者在循环体里读了不止一层的成员（`Item.A.B`），都报这个码；消息会说明是哪一种。

```dui
Widget Panel {
    RenderOpacity <- Fadee() * 0.8
    Shown <- GetName() * 2
}
```

**怎么改**：修正表达式；逻辑复杂时写进一个真正的函数，然后绑定那个函数。

<a id="dui5012"></a>

## DUI5012

**严重级别：** 错误 (`error`)

**消息：**

```text
一个放错了地方的 each：在根上、嵌套在另一个循环里、宿主没有列表视图，或者循环体不是恰好一个模板控件。
```

**触发位置：** `EachMisplaced`

**怎么会出现**：`each` 填充它所在控件的列表视图，所以宿主需要 `+ UIListView`（或可回收的滚动视图）；它不能是根，不能写在另一个 `for` / `each` 里；循环体恰好是一个控件，即每一项重复的那个模板。

```dui
Widget Inventory {
    each Item in GetItems() {
        Text Cell { Text <- Item.Name }
    }
}
```

**怎么改**：给宿主加 `+ UIListView { }`；多个控件包进一个容器；嵌套的列表写成一个组件，内层循环写在组件自己的文件里。

<a id="dui5013"></a>

## DUI5013

**严重级别：** 错误 (`error`)

**消息：**

```text
一个作为值写下、却没有指向文件里任何节点的节点 id。
```

**触发位置：** `NodeReferenceNotFound`

**怎么会出现**：持有控件、视觉对象或行为的属性可以写一个节点 id，表示文件里的那个节点；这个 id 不存在时报这个码，消息会给出最接近的 id。节点存在但不是属性要的那种东西，报的是 DUI4003。

```dui
Widget Confirm {
    + UIButton { TransitionTarget = Bakground }
    Image Background { }
}
```

**怎么改**：修正 id，或者先声明那个节点。

<a id="dui5014"></a>

## DUI5014

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写在循环体里的表达式或 <->，或者一条通往别的对象的路由。
```

**触发位置：** `LoopBodyBindingUnsupported`

**怎么会出现**：生成的绑定函数是类的函数，而循环体里的名字只有每一次迭代才知道，所以循环体里只支持两种按格逐个记录的写法：单跳的 `Item.Member` 绑定，和调用这一项自己函数的路由——`Event -> Item.Func`（把事件发出的东西交给它）或 `Event -> Item.Func()`（什么都不传）。更复杂的表达式和 `<->` 以前会被悄悄丢掉：文件编译通过，属性却从来没有被驱动。路由到 `Settings.Apply()` 这样的别的对象、走两段以上（`Item.Owner.Use`）、或者带实参，也是这一条。表达式本身没问题，问题在位置，所以它不是 DUI5011。

```dui
VerticalBox Options {
    for Option in GetOptions() {
        Text { Text <- Option.Label + "!" }
    }
}
```

**怎么改**：把逻辑放到每一项自己的类上（让它提供一个现成的成员），或者把绑定写到循环外面。

<a id="dui5015"></a>

## DUI5015

**严重级别：** 错误 (`error`)

**消息：**

```text
一条路径没有指向任何节点的轨道。
```

**触发位置：** `TimelineTargetNotFound`

**怎么会出现**：轨道行的路径是一串节点 id（`Row/Title`），和动画绑定解析时用的显示名路径相同；文件的树上没有这条路径时报这个码。节点存在但没有那个属性，报的是 DUI5016。

```dui
timeline Pulse {
    duration = 0.6
    Iocn.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1)
}

Widget Root {
    Image Icon
}
```

**怎么改**：修正路径里的 id；不写路径的轨道驱动的是动画所在的控件本身。

<a id="dui5016"></a>

## DUI5016

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有任何动画轨道能驱动的轨道属性。
```

**触发位置：** `TimelinePropertyNotAnimatable`

**怎么会出现**：属性在节点、它的视觉对象和行为上都不存在；或者它没有标记 `Interp`（动画编辑器也不会提供它）；或者它的类型没有轨道：时间轴驱动数字、二到四分量的向量和颜色，其他类型属于 `external` 时间轴。

```dui
timeline Swap {
    Title.Text : 0.0 = "A", 1.0 = "B"
}

Widget Root {
    Text Title
}
```

**怎么改**：换一个动画编辑器里能看到的属性；其他类型的动画放进一个 `external` 时间轴，在 Sequencer 里编辑。

<a id="dui5017"></a>

## DUI5017

**严重级别：** 错误 (`error`)

**消息：**

```text
一个缓动库里没有的 ease 名字。
```

**触发位置：** `UnknownEaseName`

**怎么会出现**：曲线名取自 `EDreamTweenEase`（`Linear`、`InOutQuad`、`OutCubic`、`OutBack` 等），只去掉了 `CurveFloat`，因为它指向一个资产，而时间轴的关键帧没有地方放资产。

```dui
timeline Pulse {
    Icon.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuadratic
}

Widget Root {
    Image Icon
}
```

**怎么改**：换成 `EDreamTweenEase` 里的名字，比如 `InOutQuad`。

<a id="dui5018"></a>

## DUI5018

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有指向任何类的组件别名：文件既没有 class 行也没有 Blueprint 用它，或者路径什么也加载不出来。
```

**触发位置：** `ComponentAliasUnresolved`

**怎么会出现**：`use "…" as Row` 指名的是那个文件编译成的类：它的 `class` 行写的那个，或者把它当 Source File 的那个 Blueprint。文件没有 `class` 行、也还没有 Blueprint 用它；没有编辑器可问（比如在 commandlet 里）；`class` 行指向的 Blueprint 还不存在；或者 `use /Game/… as` 的路径加载不出类，都报这个码。

```dui
use "UI/Components/Row.dui" as Row

VerticalBox Root {
    Row Audio { Label = "Audio" }
}
```

**怎么改**：给组件文件加一行 `class`，或者先把它编译进一个 Blueprint；类路径的话，修正路径。

<a id="dui5019"></a>

## DUI5019

**严重级别：** 错误 (`error`)

**消息：**

```text
一个写在非组件实例下、用来填充 slot 的 slot Name { … }。
```

**触发位置：** `SlotFillOutsideComponent`

**怎么会出现**：带控件的 `slot Name { … }` 是在填充组件的命名 slot，只能写在组件实例里面；写在根上，或写在普通控件、容器里面，就没有 slot 可填。

```dui
VerticalBox Page {
    slot Detail {
        Text Note { Text = "A length of wire." }
    }
}
```

**怎么改**：把内容直接嵌套进去；如果你是在写组件、想声明一个 slot，就写 `slot Detail`，不带控件。

<a id="dui5020"></a>

## DUI5020

**严重级别：** 错误 (`error`)

**消息：**

```text
一个指向组件没有声明的 slot 的填充。
```

**触发位置：** `UnknownSlotToFill`

**怎么会出现**：组件实例里的 `slot Name { … }` 要填的 slot 必须是那个组件声明过的；消息会列出它实际有哪些 slot。

```dui
ListPage Page_2 {
    slot Detials {
        Text Note { Text = "A length of wire." }
    }
}
```

**怎么改**：修正 slot 名；嵌套而不写 slot 名的内容会进入组件的默认 slot。

<a id="dui5021"></a>

## DUI5021

**严重级别：** 错误 (`error`)

**消息：**

```text
一个放错了地方的 for：在根上、在另一个循环里、在容量固定的宿主里，或者循环体不是恰好一个模板。
```

**触发位置：** `ForMisplaced`

**怎么会出现**：`for` 在它所在的控件里为每一项复制一份模板，所以宿主必须能放任意多个子节点：`SizeBox` 这类只放一个子节点的不行。它不能是根，不能写在另一个 `for` / `each` 里，循环体恰好是一个控件。

```dui
SizeBox Box {
    for Option in GetOptions() {
        Text { Text <- Option.Label }
    }
}
```

**怎么改**：在里面放一个 `VerticalBox` 之类的容器，把 `for` 写进容器；多个控件包进一个容器；嵌套的重复写成一个组件，内层循环写在组件自己的文件里，列表通过它的 `props` 传进去。

<a id="dui5022"></a>

## DUI5022

**严重级别：** 错误 (`error`)

**消息：**

```text
一个节点上的第二个布局容器。
```

**触发位置：** `SecondLayoutContainer`

**怎么会出现**：一个控件只用一种方式排列它的子节点。节点类型本身已经是容器（`VerticalBox Column`），又写了 `+ HorizontalBox`；或者它自己的 `+` 行和样式的 `+` 行各带来一个容器。以前第二个会一声不响地替换掉第一个，连同它的值一起。

```dui
VerticalBox Column {
    + HorizontalBox { Spacing = 8 }
}
```

**怎么改**：容器的属性直接写在节点上；需要另一种排列，就嵌套一个子控件。

<a id="dui5023"></a>

## DUI5023

**严重级别：** 错误 (`error`)

**消息：**

```text
成员路径里的某一段，在它所在的类上找不到，或者 Blueprint 看不到。
```

**触发位置：** `MemberPathNotFound`

**怎么会出现**：`Player.Name` 这样的路径从类的一个变量开始——`viewmodels` 条目、prop 或者 Blueprint 变量（以上一次编译为准）——后面每一段都是前一段所持对象的类上的成员。某一段不存在，或者存在但图表够不着——属性不是 `BlueprintReadOnly` / `BlueprintReadWrite`，函数不是 `BlueprintCallable` / `BlueprintPure`——就是这一条，消息会点出是哪一段、在哪个类上。UnrealSharp 的属性只有在 `[UProperty]` 写了 `BlueprintReadOnly` 或 `BlueprintReadWrite` 时才算 Blueprint 可见。

```dui
viewmodels {
    PlayerVM Player
}

VerticalBox Root {
    Text Name { Text <- Player.Nmae }
}
```

**怎么改**：修正成员名；C++ 的属性加上 `BlueprintReadOnly`，函数加上 `BlueprintCallable` 或 `BlueprintPure`。

<a id="dui5024"></a>

## DUI5024

**严重级别：** 错误 (`error`)

**消息：**

```text
一条越过非对象的值继续往下走的成员路径。
```

**触发位置：** `MemberPathThroughNonObject`

**怎么会出现**：路径的每一段都从前一段持有的**对象**上读。`Player.Health.Max` 里 `Health` 是一个 float，它上面没有可读的成员。结构体也一样：路径只穿过对象引用。

```dui
viewmodels {
    PlayerVM Player
}

VerticalBox Root {
    Text Hp { Text <- Player.Health.Max }
}
```

**怎么改**：让路径停在值那里；需要的派生值由视图模型作为一个成员提供（比如 `MaxHealth`）。

<a id="dui5025"></a>

## DUI5025

**严重级别：** 错误 (`error`)

**消息：**

```text
事件接不住的路由运算符：单播委托上的 +=，或者多播事件上的 =。
```

**触发位置：** `RouteOperatorMismatch`

**怎么会出现**：`+=` 是在其他监听者旁边再加一个，所以只能用在能装多个监听者的事件上（`BlueprintAssignable` 多播委托、`DreamUIEventDelegate`）；`=` 是成为事件唯一的监听者，只能用在单播委托上——用在多播事件上，就得扔掉控件自己和 Blueprint 图表接上去的监听者。`->` 按事件的种类二者都行，永远不会是这一条。报了这一条的路由不会被记录：换一种方式接上，做的就不是这一行写的事了。

```dui
VerticalBox Root {
    Native.Button Apply { OnClicked = HandleApply }
}
```

**怎么改**：多播事件用 `+=`，单播委托用 `=`；或者两种都写 `->`。

<a id="dui6xxx"></a>

# DUI6xxx · 编译

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui6001"></a>

## DUI6001

**严重级别：** 错误 (`error`)

**消息：**

```text
Blueprint 的 Source File 指向一个不存在或读不出来的文件。
```

**触发位置：** `SourceFileUnreadable`

**怎么会出现**：编译时按 Source File 找 `.dui`：相对路径依次在项目和各个插件的 `DUI/` 根下查找，`Plugin.Name:` 前缀指定一个插件的 `DUI/`，绝对路径照原样使用。消息会写出你写的路径、实际查找的位置和搜索过的根；`Plugin.Name:` 指向的插件没有启用或没有 `DUI/` 目录时也会单独说明。

**怎么改**：把文件放到某个 `DUI/` 根下（不是 `Content/`），或者修正 Blueprint 的 Source File。

<a id="dui6002"></a>

## DUI6002

**严重级别：** 错误 (`error`)

**消息：**

```text
一个解析通过、却没有产生任何树的文件。
```

**触发位置：** `EmptyTree`

**怎么会出现**：构建器没有交回控件树。原因（比如某个节点类型解析不到）在同一次编译里已经由更早的诊断说过了，这一条报的是结果：没有东西可以编译进这个类。

**怎么改**：先处理同一次编译里更早的那几条诊断。

<a id="dui6003"></a>

## DUI6003

**严重级别：** 警告 (`warning`)

**消息：**

```text
一个指向了另一个 Blueprint、而不是正在编译的这个的 class 行（警告）。
```

**触发位置：** `ClassPathMismatch`

**怎么会出现**：文件的 `class` 行写的资产不是当前正在编译的 Blueprint，比如复制了一个文件却没改 `class` 行。它只是警告：这一行的任务是提供稳定的本地化命名空间，写错只会让键漂移，不会破坏构建；本地化键仍然使用文件里写的名字。

```dui
class /Game/UI/WBP_Settings

Widget Root { }
```

**怎么改**：把 `class` 行改成正在编译的 Blueprint 的路径；如果文件是有意在多个子类之间共享的，可以忽略这条警告。

<a id="dui6004"></a>

## DUI6004

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 -> Handler，而类里没有这个函数。
```

**触发位置：** `EventHandlerNotFound`

**怎么会出现**：处理函数在这次编译正在构建的类上，构建器没法提前查找，所以由编译器检查：这个 Blueprint 上找不到这个名字的函数。事件这一半出错报的是 DUI5010。

```dui
Widget Confirm {
    + UIButton { OnClick -> HandleConfrm }
}
```

**怎么改**：修正函数名，或者在 Blueprint 里加上这个函数。

<a id="dui6005"></a>

## DUI6005

**严重级别：** 错误 (`error`)

**消息：**

```text
一个参数与事件发出的参数不一致的处理函数。
```

**触发位置：** `EventHandlerSignatureMismatch`

**怎么会出现**：函数找到了，但它的参数不是事件发出的那些（个数或类型不对）。

**怎么改**：让函数的参数与事件的签名一致，或者换一个签名匹配的函数。

<a id="dui6006"></a>

## DUI6006

**严重级别：** 错误 (`error`)

**消息：**

```text
一个类里既没有同名函数、也没有同名变量的循环来源。
```

**触发位置：** `EachSourceNotFound`

**怎么会出现**：`in GetOptions()` 需要这个类上有一个同名的无参函数，`in Options` 需要一个同名的数组变量。来源在这次编译正在构建的类上，所以这个检查在编译阶段做。

```dui
VerticalBox Options {
    for Option in GetOptions() {
        Text { Text <- Option.Label }
    }
}
```

**怎么改**：在 Blueprint 里声明这个函数或变量，或者修正名字。

<a id="dui6007"></a>

## DUI6007

**严重级别：** 错误 (`error`)

**消息：**

```text
一个提供的不是对象数组的循环来源。
```

**触发位置：** `EachSourceNotObjectArray`

**怎么会出现**：来源存在，但它给出的不是对象数组。每一项的绑定通过反射读 `Item.Member`，元素必须有成员可读：`TArray<UMyRow*>` 可以，`TArray<FMyRow>` 和 `TArray<int32>` 不行。名字是对的，错的是类型，所以它不是 DUI6006。

**怎么改**：让来源返回对象数组，比如把每一行做成一个 `UObject` 子类。

<a id="dui6008"></a>

## DUI6008

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没有对应 Blueprint 引脚类型的 props 或 events 类型。
```

**触发位置：** `PropTypeUnknown`

**怎么会出现**：属性和事件参数的类型只能是 `Text`、`String`、`Number`、`Integer`、`Bool`、`Color`、`Vector2`、`Asset`、`Class`，或者 `Enum` 加枚举路径。拼错的类型名、`Enum` 后面没有路径、路径上没有枚举、枚举不是 `BlueprintType`，都报这个码。

```dui
props {
    Float Speed = 1
}

Widget Root { }
```

**怎么改**：换成上面列出的类型（这里是 `Number`）；枚举要可以作为 Blueprint 类型使用。

<a id="dui6009"></a>

## DUI6009

**严重级别：** 错误 (`error`)

**消息：**

```text
一个已经被另一个类型不同的成员占用、或者与某个控件 id 相同的 props 名字。
```

**触发位置：** `PropNameTaken`

**怎么会出现**：每个属性会变成类的一个变量，名字不能已经被占用：一个控件 id（每个控件都有同名的类变量）、一个资源条目、这个 Blueprint 自己声明的成员、父类的函数。C++ 父类上有同名同类型的属性时会直接用它，类型不同则报这个码。

```dui
props {
    Text Title
}

VerticalBox Root {
    Text Title { Text <- Title }
}
```

**怎么改**：给其中一个改名；想用父类的属性，就按它的类型声明。

<a id="dui6010"></a>

## DUI6010

**严重级别：** 错误 (`error`)

**消息：**

```text
一个 emit Name，而这个文件没有声明这个事件。
```

**触发位置：** `EmitUnknownEvent`

**怎么会出现**：`emit` 只能引发它自己文件的 `events` 块里声明的事件；块里没有这个名字，或者文件根本没有 `events` 块。

```dui
events {
    Picked(Number Index)
}

Widget Row {
    + UIButton { OnClick -> emit Pickd(0) }
}
```

**怎么改**：修正事件名，或者在 `events` 块里声明它。

<a id="dui6011"></a>

## DUI6011

**严重级别：** 错误 (`error`)

**消息：**

```text
一个实参与事件参数不一致的 emit。
```

**触发位置：** `EmitArgumentMismatch`

**怎么会出现**：`emit Name(…)` 的实参个数要等于事件声明的参数个数，每个实参的类型也要能作为对应参数的类型。

```dui
events {
    Picked(Number Index)
}

Widget Row {
    + UIButton { OnClick -> emit Picked() }
}
```

**怎么改**：按事件的声明传实参，比如 `emit Picked(ValueIndex)`。

<a id="dui6012"></a>

## DUI6012

**严重级别：** 错误 (`error`)

**消息：**

```text
一个无法生成处理函数的 emit：在循环体里，或者事件的参数 Blueprint 无法承载。
```

**触发位置：** `EmitRouteUnsupported`

**怎么会出现**：`-> emit` 会被编译成一个生成的处理函数。`for` / `each` 循环体里的控件是循环的副本而不是类的成员，那里放不下这样的函数；路由所在的事件如果发出的参数 Blueprint 函数接不住（比如一个 Blueprint 没有对应引脚的 `FDreamUIEventDelegate`），也生成不了。这里直接报出来，而不是让路由悄悄消失。

```dui
events {
    Picked(Number Index)
}

VerticalBox Options {
    for Option in GetOptions() {
        Widget {
            + UIButton { OnClick -> emit Picked(0) }
        }
    }
}
```

**怎么改**：把事件路由到类的一个函数，在那个函数里引发这个事件；或者换一个参数 Blueprint 能接住的事件。

<a id="dui6013"></a>

## DUI6013

**严重级别：** 错误 (`error`)

**消息：**

```text
一个声明的类型容纳不了的 props 默认值。
```

**触发位置：** `PropDefaultInvalid`

**怎么会出现**：默认值要符合属性的类型：`Text` 和 `String` 是带引号的字符串，`Integer` 是整数，`Bool` 是 `true` 或 `false`，`Color` 是十六进制颜色，`Vector2` 是二元组，枚举要写它声明过的值，资产要真的存在，`@Name` 要指向一个资源条目。默认值被丢弃，否则这个变量会让读它的每个绑定和每一行宿主代码都失败。

```dui
props {
    Integer Count = 1.5
}

Widget Root { }
```

**怎么改**：按类型改正默认值，比如 `Integer Count = 1`。

<a id="dui6014"></a>

## DUI6014

**严重级别：** 错误 (`error`)

**消息：**

```text
一个已经被类的另一个成员占用的 events 名字。
```

**触发位置：** `EventNameTaken`

**怎么会出现**：每个事件会变成一个事件分发器，名字不能已经属于：一个属性、一个控件、一个资源、这个 Blueprint 自己的变量或函数，或者父类上一个不是同参数分发器的成员。父类上同名同参数的分发器会直接拿来用。

```dui
events {
    Title
}

VerticalBox Root {
    Text Title { Text = "Settings" }
}
```

**怎么改**：给事件或另一个成员改名。

<a id="dui6015"></a>

## DUI6015

**严重级别：** 错误 (`error`)

**消息：**

```text
viewmodels 里的一个类型，找不到类，或者找到不止一个。
```

**触发位置：** `ViewModelClassUnknown`

**怎么会出现**：类型可以是类的反射名（C++ 的 `UPlayerVM` 写作 `PlayerVM`）、完整路径（`/Script/MyGame.PlayerVM`、`/Game/UI/BP_PlayerVM`），或者 `use … as` 给的名字。短名字对不上任何类——拼错了、模块没加载、Blueprint 还不存在——是这一条；两个模块里各有一个同名的类时也是，编译器不替你挑。

```dui
viewmodels {
    PlayrVM Player
}
```

**怎么改**：修正类名；有歧义时写完整路径，或者写 `use /Script/MyGame.PlayerVM as PlayerVM`，再用这个名字。

<a id="dui6016"></a>

## DUI6016

**严重级别：** 错误 (`error`)

**消息：**

```text
一个已经被类的另一个成员占用的 viewmodels 名字。
```

**触发位置：** `ViewModelNameTaken`

**怎么会出现**：每个条目会变成类上的一个变量，所以名字不能已经属于一个控件、一个 prop、一个资源、一个事件，或者这个 Blueprint 自己的变量和函数。父类上有同名成员时分两种情况：父类是 C++ 类、那个成员正好是这个类的对象引用，这一行就是在描述那个属性（它的对象从哪来），不另建变量；父类自己的 `.dui` 已经声明过这个条目，或者同名成员不是这个类的引用，就是这一条。

```dui
viewmodels {
    PlayerVM Title
}

VerticalBox Root {
    Text Title { Text = "Status" }
}
```

**怎么改**：给条目或另一个成员改名；要沿用父类 `.dui` 声明的条目，删掉这一行，子类会原样读它。

<a id="dui6017"></a>

## DUI6017

**严重级别：** 错误 (`error`)

**消息：**

```text
一个没法兑现的来源：对抽象类写 = new。
```

**触发位置：** `ViewModelSourceInvalid`

**怎么会出现**：`= new` 让控件在没被交到对象时自己建一个，而抽象类没有东西可建。`UDreamViewModel` 自己就是抽象的，要写的是它的子类。报错之后这个条目仍然是类上的一个变量，经过它的绑定照常编译，只是运行时不会去填它。（`global ""` 这样的空名字在解析时就已经是 DUI2021。）

```dui
viewmodels {
    DreamViewModel State = new
}
```

**怎么改**：写一个具体的类；或者去掉 `= new`，由宿主把对象交过来。

<a id="dui6018"></a>

## DUI6018

**严重级别：** 错误 (`error`)

**消息：**

```text
-> Path.Func 的函数，在对象的类上没有，或者 Blueprint 不能调用。
```

**触发位置：** `RouteMemberFunctionNotFound`

**怎么会出现**：路由右边的路径走到一个对象，最后一段是这个对象的类上的函数。函数不存在、不是 `BlueprintCallable`，或者是 `BlueprintPure`（纯函数只返回一个值，事件没有什么要它做的），都是这一条。

```dui
viewmodels {
    SettingsVM Settings
}

VerticalBox Root {
    Native.Button Apply { OnClicked += Settings.Aply() }
}
```

**怎么改**：修正函数名；C++ 的函数写成 `UFUNCTION(BlueprintCallable)`。

<a id="dui6019"></a>

## DUI6019

**严重级别：** 错误 (`error`)

**消息：**

```text
-> Path.Func 的实参和函数对不上。
```

**触发位置：** `RouteArgumentMismatch`

**怎么会出现**：带括号时，括号里就是实参：类上的表达式，事件自己的参数按名字可用；个数和类型要和函数的参数一致。不带括号时，函数要么什么都不收，要么收的恰好是事件发出的那些。

```dui
viewmodels {
    SettingsVM Settings
}

VerticalBox Root {
    Native.Slider Volume { OnValueChanged += Settings.SetVolume(Value, 1) }
}
```

**怎么改**：按函数的参数传实参；或者去掉括号，让函数直接收事件的参数。

<a id="dui6020"></a>

## DUI6020

**严重级别：** 错误 (`error`)

**消息：**

```text
<-> Path.Member 的成员写不回去。
```

**触发位置：** `TwoWayTargetReadOnly`

**怎么会出现**：`<->` 要把控件的变化写回这个成员：对象的类上有 `BlueprintCallable` 的 `Set<Member>`（收一个同类型的参数）时调用它，否则直接写属性并在对象上广播。属性对图表只读（`BlueprintReadOnly`）又没有这样的 setter 时，两条路都走不通——滑条会动一下又弹回去——所以编译时就拒绝。

```dui
viewmodels {
    SettingsVM Settings
}

VerticalBox Root {
    Native.Slider Volume { Value <-> Settings.MasterVolume }
}
```

这里 `MasterVolume` 是 `BlueprintReadOnly`，`SettingsVM` 上也没有 `SetMasterVolume`。

**怎么改**：给类加一个 `BlueprintCallable` 的 `SetMasterVolume(float)`；或者把属性改成 `BlueprintReadWrite`；只需要单向时写 `<-`。

<a id="dui6021"></a>

## DUI6021

**严重级别：** 错误 (`error`)

**消息：**

```text
循环体里一个元素类上没有的 Item.Member。
```

**触发位置：** `LoopItemMemberNotFound`

**怎么会出现**：循环源是某个具体类的对象数组（`TArray<UItemVM*>`）时，编译器知道每一项是什么类，就拿它来查 `Item.Member`。源是 `TArray<UObject*>` 时元素的类要到运行时才知道，这一行留给运行时去查，对不上的副本照旧静默跳过。

```dui
viewmodels {
    InventoryVM Inventory
}

VerticalBox Root {
    for Item in Inventory.Items {
        Text { Text <- Item.Nmae }
    }
}
```

**怎么改**：修正成员名，或者给元素类加上这个成员。

<a id="dui6022"></a>

## DUI6022

**严重级别：** 错误 (`error`)

**消息：**

```text
循环体里一个 -> Item.Func，元素类上没有这个函数，或者参数对不上。
```

**触发位置：** `LoopItemRouteMismatch`

**怎么会出现**：元素类已知时，`Event -> Item.Func` 和 `Event -> Item.Func()` 要求函数在元素类上，并且要么什么都不收，要么收的恰好是事件发出的那些。

```dui
viewmodels {
    InventoryVM Inventory
}

VerticalBox Root {
    for Item in Inventory.Items {
        Native.Button { OnClicked += Item.Equip() }
    }
}
```

这里元素类 `ItemVM` 上没有 `Equip`。

**怎么改**：修正函数名或参数；需要额外的实参时，让元素类提供一个不带参数的版本。

<a id="dui7xxx"></a>

# DUI7xxx · 写回

本页由 `scripts/diagnostics/sync.py` 从插件的码表（`EDreamUIDiagnosticCode`）生成，卡片里的枚举名就是测试断言用的那个名字。

<a id="dui7001"></a>

## DUI7001

**严重级别：** 错误 (`error`)

**消息：**

```text
设计器的一次编辑在文件里找不到落脚处：未知的节点、被绑定的属性、没有块的 slot。
```

**触发位置：** `PatchTargetNotFound`

**怎么会出现**：设计器要把一次修改写进文本，却找不到可以写的地方：文件里没有这个名字的节点、样式或资源；属性被 `<-` 或 `Shown <-` 绑定着，写回的值会悄悄替换掉绑定或者被它覆盖；`slot Name` 没有块，没有地方写它的属性；要插入的节点 id 已经存在；要删除或移动的是根节点，或者是某个循环的模板；文件根本没有解析成树。什么都不会被写入。

**怎么改**：按消息去改文本本身：比如给 slot 加上 `{ }`，或者先去掉绑定，再在设计器里修改。

<a id="dui7002"></a>

## DUI7002

**严重级别：** 错误 (`error`)

**消息：**

```text
文件在一次待写入的编辑底下被改动了，于是什么都没有写入。
```

**触发位置：** `SourceFileChangedUnderEdit`

**怎么会出现**：设计器的编辑是针对它上一次解析出的树计划的，写入前发现磁盘上的文本已经和那棵树对不上（比如你在另一个编辑器里改了文件），或者两处编辑落在同一段字符上。设计器从不覆盖在磁盘上被改过的文件。

**怎么改**：让编辑器重新读取文件（保存后会自动重新编译），然后在设计器里再做一次这个修改。

<a id="dui7003"></a>

## DUI7003

**严重级别：** 错误 (`error`)

**消息：**

```text
一个这门语言写不出来的值（非有限的数），它所在的那一行保持原样。
```

**触发位置：** `PatchValueNotRepresentable`

**怎么会出现**：打印器会像 printf 那样把非有限的浮点数写成 `inf` 之类，而 `inf` 会被读成一个标识符：文件能解析，下一次编译却会在一行谁也没碰过的地方报错。所以在写入时就拒绝。枚举字节的值不在枚举声明里时也一样。文件本身没有问题，所以它不借用词法的 DUI1004。

**怎么改**：在设计器里给这个属性一个有限的值，或者直接在文本里写。

<a id="dui7004"></a>

## DUI7004

**严重级别：** 错误 (`error`)

**消息：**

```text
设计器要改的东西没有哪一行写着它：由 if 决定的可见性，或者无法改写的简写。
```

**触发位置：** `PatchSyntaxNotWritable`

**怎么会出现**：有些值不是写在某一行上，而是前端读出来时产生的，设计器不会在作者没写过的地方发明一行：`if` 分支里控件的可见性；`@fill 2` 要改成 Fill 以外的 `SizeRule`，或者一个 `@fill` 写不出来的权重；`rows` 表某一行上除了列值和它自己块里的行之外的任何东西（行不能在设计器里删除、移动、改名或加 `+` 块）。

```dui
Widget Root {
    if HasSave() {
        Text Continue { Text = "Continue" }
    }
}
```

**怎么改**：去改文本：改 `if` 的条件或者把控件移出分支；把 `@fill 2` 换成 `@slot SizeRule = …` 和 `@slot FillWeight = 2`；`rows` 的行直接在表里编辑。

<a id="dui7005"></a>

## DUI7005

**严重级别：** 错误 (`error`)

**消息：**

```text
设计器要改的值来自样式的 + 行；要么给节点加上它自己的那一行，要么去改样式。
```

**触发位置：** `PatchStyleComponentNotWritable`

**怎么会出现**：节点的某个值来自它的样式里的 `+ Component { … }`，而节点自己没有写同一个类的 `+` 行。这个值有两个可能的去处，写回哪个都不对：样式是共享的，写进样式会改动每个戴它的节点；给节点新增一个 `+` 块是在改文件的结构，而值的写回不做这种改动。

```dui
style RowColumn {
    + VerticalBox { Spacing = 15 }
}

Widget Left : RowColumn { }
```

**怎么改**：在节点上写一行自己的 `+ VerticalBox { Spacing = 20 }`，或者直接修改样式。
