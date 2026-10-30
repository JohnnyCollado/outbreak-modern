package com.outbreakmodern.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.LocalTextStyle
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.draw.scale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// The settings shell shared by the launcher and the in-game menu: a tab list, the current tab's rows, a footer with
// button prompts and the two actions (Quit / Play, or Quit game / Resume). Navigation is the developer's "step in"
// model (2026-10-26): focus starts on the tab list (up/down or L1/R1 change tabs, the content previews); A steps into
// the tab (its first row, or the row focused there last); inside, up/down move between rows, left/right change a
// value, A acts; B steps back out to the tab list. Down past the last tab reaches the footer actions (left/right
// between them, A acts, up or B back to the last tab). B on the tab list = the screen's back action. Row focus is
// restored per tab, as in PlayFieldPortal's SettingsScaffold (feature-settings/ui/SettingsScaffold.kt).

val Accent = Color(0xFF5B8DEF)
private val Bg = Color(0xFF0F1115)
private val Panel = Color(0xFF171A20)
private val PanelHi = Color(0xFF222733)
private val TextHi = Color(0xFFE8EAEE)
private val TextLo = Color(0xFF98A0AE)
val Good = Color(0xFF5BC27A)
val Bad = Color(0xFFE5735F)

val AppColors = darkColorScheme(primary = Accent, background = Bg, surface = Panel, onSurface = TextHi)

enum class Tone { NORMAL, MUTED, GOOD, BAD }

sealed class SRow(open val key: String) {
    open val focusable: Boolean get() = false

    data class Header(override val key: String, val text: String) : SRow(key)
    data class Info(override val key: String, val text: String, val tone: Tone = Tone.NORMAL) : SRow(key)
    data class Action(
        override val key: String, val label: String, val value: String = "", val enabled: Boolean = true,
        val onA: () -> Unit,
    ) : SRow(key) {
        override val focusable get() = enabled
    }
    data class Choice(
        override val key: String, val label: String, val options: List<String>, val index: Int,
        val enabled: Boolean = true, val onPick: (Int) -> Unit,
    ) : SRow(key) {
        override val focusable get() = enabled
    }
    data class Toggle(
        override val key: String, val label: String, val on: Boolean, val enabled: Boolean = true,
        val onSet: (Boolean) -> Unit,
    ) : SRow(key) {
        override val focusable get() = enabled
    }
    /** A row with a label, a value and several buttons: left/right pick a button, A presses it (touch: tap it). */
    data class Buttons(
        override val key: String, val label: String, val value: String, val buttons: List<String>,
        val badge: String = "", val danger: Set<Int> = emptySet(), val onPress: (Int) -> Unit,
    ) : SRow(key) {
        override val focusable get() = buttons.isNotEmpty()
    }
    data class Slider(
        override val key: String, val label: String, val value: Int, val min: Int, val max: Int, val step: Int,
        val suffix: String, val enabled: Boolean = true, val onSet: (Int) -> Unit,
    ) : SRow(key) {
        override val focusable get() = enabled
    }
}

data class SPage(val title: String, val rows: List<SRow>)

/** A two-choice prompt over the screen; left/right choose, A confirms, B = the second choice. */
data class Confirm(val title: String, val body: String, val yes: String, val no: String, val onYes: () -> Unit,
                   val onNo: () -> Unit = {})

/** A running import: label, progress 0..1 (negative = counting), detail text, and its cancel. */
data class Progress(val label: String, val fraction: Float, val detail: String, val onCancel: () -> Unit)

enum class Level { LIST, PAGE, FOOTER }

class MenuNav {
    var level by mutableStateOf(Level.LIST)
    var tab by mutableIntStateOf(0)
    var footer by mutableIntStateOf(1)
    val rowKey = mutableStateMapOf<String, String>() // focused row key per tab title
    val buttonIndex = mutableStateMapOf<String, Int>() // the picked button of each Buttons row
    var confirm by mutableStateOf<Confirm?>(null)
    var confirmIndex by mutableIntStateOf(1)
    var progress by mutableStateOf<Progress?>(null)

    fun ask(c: Confirm) {
        confirm = c
        confirmIndex = 1 // the safe choice
    }

    private fun focusables(page: SPage) = page.rows.filter { it.focusable }

    fun focusedRow(page: SPage): SRow? {
        val f = focusables(page)
        return f.firstOrNull { it.key == rowKey[page.title] } ?: f.firstOrNull()
    }

    /** One controller / key action. footerActions: (index) -> run; onBackFromList: B on the tab list; onStart: Start. */
    fun handle(a: Nav, pages: List<SPage>, footerActions: (Int) -> Unit, onBackFromList: () -> Unit, onStart: () -> Unit) {
        confirm?.let { c ->
            when (a) {
                Nav.LEFT, Nav.RIGHT, Nav.UP, Nav.DOWN -> confirmIndex = 1 - confirmIndex
                Nav.A -> { confirm = null; if (confirmIndex == 0) c.onYes() else c.onNo() }
                Nav.B -> { confirm = null; c.onNo() }
                else -> {}
            }
            return
        }
        progress?.let { p ->
            if (a == Nav.A || a == Nav.B)
                ask(Confirm("Cancel the import?", p.label, "Cancel import", "Keep going", p.onCancel))
            return
        }
        if (pages.isEmpty()) return
        tab = tab.coerceIn(0, pages.size - 1)
        val page = pages[tab]
        when (level) {
            Level.LIST -> when (a) {
                Nav.UP -> if (tab > 0) tab--
                Nav.DOWN -> if (tab < pages.size - 1) tab++ else level = Level.FOOTER.also { footer = 1 }
                Nav.L1 -> tab = (tab + pages.size - 1) % pages.size
                Nav.R1 -> tab = (tab + 1) % pages.size
                Nav.A, Nav.RIGHT -> if (focusables(page).isNotEmpty()) level = Level.PAGE
                Nav.B -> onBackFromList()
                Nav.START -> onStart()
                else -> {}
            }
            Level.PAGE -> {
                val f = focusables(page)
                val cur = focusedRow(page)
                val i = f.indexOf(cur).coerceAtLeast(0)
                when (a) {
                    Nav.UP -> if (i > 0) rowKey[page.title] = f[i - 1].key
                    Nav.DOWN -> if (i < f.size - 1) rowKey[page.title] = f[i + 1].key
                    Nav.LEFT -> change(cur, -1)
                    Nav.RIGHT -> change(cur, +1)
                    Nav.A -> activate(cur)
                    Nav.B -> level = Level.LIST
                    Nav.L1 -> tab = (tab + pages.size - 1) % pages.size
                    Nav.R1 -> tab = (tab + 1) % pages.size
                    Nav.START -> onStart()
                    else -> {}
                }
                if ((a == Nav.L1 || a == Nav.R1) && focusables(pages[tab]).isEmpty()) level = Level.LIST
            }
            Level.FOOTER -> when (a) {
                Nav.LEFT -> footer = 0
                Nav.RIGHT -> footer = 1
                Nav.UP, Nav.B -> { level = Level.LIST; tab = pages.size - 1 }
                Nav.A -> footerActions(footer)
                Nav.START -> onStart()
                else -> {}
            }
        }
    }

    private fun change(row: SRow?, dir: Int) {
        when (row) {
            is SRow.Choice -> row.onPick((row.index + dir + row.options.size) % row.options.size)
            is SRow.Toggle -> row.onSet(dir > 0)
            is SRow.Slider -> row.onSet((row.value + dir * row.step).coerceIn(row.min, row.max))
            is SRow.Buttons -> buttonIndex[row.key] = ((buttonIndex[row.key] ?: 0) + dir).coerceIn(0, row.buttons.size - 1)
            else -> {}
        }
    }

    private fun activate(row: SRow?) {
        when (row) {
            is SRow.Buttons -> row.onPress((buttonIndex[row.key] ?: 0).coerceIn(0, row.buttons.size - 1))
            is SRow.Action -> row.onA()
            is SRow.Choice -> row.onPick((row.index + 1) % row.options.size)
            is SRow.Toggle -> row.onSet(!row.on)
            else -> {}
        }
    }
}

@Composable
fun SettingsShell(
    title: String, subtitle: String, pages: List<SPage>, nav: MenuNav,
    footerLabels: List<String>, footerDim: List<Boolean>, footerActions: (Int) -> Unit,
    status: String, backLabel: String, startLabel: String?,
    modifier: Modifier = Modifier,
    backdrop: Color = Bg,
) {
    val tab = nav.tab.coerceIn(0, (pages.size - 1).coerceAtLeast(0))
    Box(modifier.fillMaxSize().background(backdrop)) {
        Column(Modifier.fillMaxSize().padding(horizontal = 28.dp, vertical = 18.dp)) {
            Text(title, color = TextHi, fontSize = 24.sp, fontWeight = FontWeight.SemiBold)
            Text(subtitle, color = TextLo, fontSize = 14.sp)
            Spacer(Modifier.height(14.dp))
            Row(Modifier.weight(1f).fillMaxWidth()) {
                // Tab list.
                // Scrolls so the selected tab stays in view (the list is taller than a 1080p handheld's panel).
                val tabScroll = androidx.compose.foundation.rememberScrollState()
                val density = androidx.compose.ui.platform.LocalDensity.current
                LaunchedEffect(tab) {
                    val rowPx = with(density) { 62.dp.toPx() }
                    val top = (tab * rowPx).toInt()
                    val bottom = top + rowPx.toInt()
                    if (top < tabScroll.value) tabScroll.animateScrollTo(top)
                    else if (bottom > tabScroll.value + tabScroll.viewportSize) tabScroll.animateScrollTo(bottom - tabScroll.viewportSize)
                }
                Column(
                    Modifier.width(250.dp).fillMaxHeight().background(Panel, RoundedCornerShape(14.dp)).padding(8.dp)
                        .verticalScroll(tabScroll)
                ) {
                    pages.forEachIndexed { i, p ->
                        val selected = i == tab
                        val focused = selected && nav.level == Level.LIST
                        Box(
                            Modifier.fillMaxWidth().padding(vertical = 2.dp)
                                .background(
                                    when {
                                        focused -> Accent
                                        selected -> PanelHi
                                        else -> Color.Transparent
                                    }, RoundedCornerShape(10.dp)
                                )
                                .clickable { nav.tab = i; nav.level = Level.LIST }
                                .heightIn(min = 48.dp)
                                .padding(horizontal = 16.dp),
                            contentAlignment = Alignment.CenterStart,
                        ) {
                            // Centred on the row by the text's ink, not its font padding (developer, 2026-10-26).
                            Text(p.title, color = if (focused) Color.White else if (selected) TextHi else TextLo, fontSize = 17.sp,
                                style = LocalTextStyle.current.copy(
                                    platformStyle = PlatformTextStyle(includeFontPadding = false),
                                    lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both),
                                ))
                        }
                    }
                }
                Spacer(Modifier.width(16.dp))
                // The tab's rows.
                val page = pages.getOrNull(tab)
                Box(Modifier.weight(1f).fillMaxHeight().background(Panel, RoundedCornerShape(14.dp)).padding(8.dp)) {
                    if (page != null) PageRows(page, nav)
                }
            }
            Spacer(Modifier.height(12.dp))
            // Footer: prompts / status left, the two actions right.
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(status, color = TextHi, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    val prompts = when (nav.level) {
                        Level.LIST -> "A  open      ▲▼ / L1 R1  tab      B  $backLabel" + (startLabel?.let { "      Start  $it" } ?: "")
                        Level.PAGE -> "▲▼  move      ◀▶  change      A  select      B  back"
                        Level.FOOTER -> "◀▶  choose      A  select      B  back"
                    }
                    Text(prompts, color = TextLo, fontSize = 13.sp)
                }
                footerLabels.forEachIndexed { i, label ->
                    val focused = nav.level == Level.FOOTER && nav.footer == i
                    val primary = i == footerLabels.size - 1
                    // Focus must read at a glance (developer, 2026-10-26): a focused button is bright accent with a
                    // thick white border, a soft accent ring outside it and a slightly larger size; unfocused ones are
                    // plain panel buttons (Play keeps only accent text), so the two states never look alike.
                    Box(
                        Modifier.padding(start = 12.dp)
                            .scale(if (focused) 1.08f else 1f)
                            .border(if (focused) 4.dp else 0.dp, if (focused) Accent.copy(alpha = 0.45f) else Color.Transparent,
                                RoundedCornerShape(14.dp))
                            .padding(if (focused) 4.dp else 0.dp)
                            .background(if (focused) Accent else PanelHi, RoundedCornerShape(10.dp))
                            .border(if (focused) 3.dp else 1.dp, if (focused) Color.White else Color.White.copy(alpha = 0.10f),
                                RoundedCornerShape(10.dp))
                            .clickable { nav.level = Level.FOOTER; nav.footer = i; footerActions(i) }
                            .padding(horizontal = 30.dp, vertical = 12.dp)
                    ) {
                        Text(label, fontSize = 17.sp,
                            fontWeight = if (focused) FontWeight.Bold else FontWeight.Normal,
                            color = when {
                                focused -> Color.White
                                footerDim[i] -> TextLo
                                primary -> Accent
                                else -> TextHi
                            })
                    }
                }
            }
        }
        nav.progress?.let { ProgressPanel(it) }
        nav.confirm?.let { ConfirmPanel(it, nav) }
    }
}

@Composable
private fun PageRows(page: SPage, nav: MenuNav) {
    val focused = if (nav.level == Level.PAGE) nav.focusedRow(page) else null
    val state = rememberLazyListState()
    val index = page.rows.indexOfFirst { it.key == focused?.key }
    LaunchedEffect(page.title, index) { if (index >= 0) state.animateScrollToItem((index - 2).coerceAtLeast(0)) }
    LazyColumn(state = state, modifier = Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        itemsIndexed(page.rows, key = { _, r -> r.key }) { _, r ->
            RowView(r, focused?.key == r.key, (nav.buttonIndex[r.key] ?: 0), onPick = { nav.buttonIndex[r.key] = it }) {
                nav.level = Level.PAGE
                nav.rowKey[page.title] = r.key
            }
        }
    }
}

@Composable
private fun RowView(r: SRow, focused: Boolean, picked: Int = 0, onPick: (Int) -> Unit = {}, onTouch: () -> Unit) {
    when (r) {
        is SRow.Buttons -> Row(
            Modifier.fillMaxWidth().background(if (focused) PanelHi else Color.Transparent, RoundedCornerShape(10.dp))
                .border(if (focused) 2.dp else 0.dp, if (focused) Accent else Color.Transparent, RoundedCornerShape(10.dp))
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(r.label, color = TextHi, fontSize = 17.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f, fill = false))
                    if (r.badge.isNotEmpty()) Text(r.badge, color = Good, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(start = 10.dp).background(Good.copy(alpha = 0.15f), RoundedCornerShape(6.dp))
                            .padding(horizontal = 8.dp, vertical = 2.dp))
                }
                if (r.value.isNotEmpty()) Text(r.value, color = TextLo, fontSize = 14.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
            r.buttons.forEachIndexed { i, label ->
                val on = focused && picked.coerceIn(0, r.buttons.size - 1) == i
                val danger = i in r.danger
                Box(
                    Modifier.padding(start = 8.dp)
                        .background(if (on) (if (danger) Bad else Accent) else Panel, RoundedCornerShape(8.dp))
                        .border(1.dp, if (on) Color.Transparent else if (danger) Bad.copy(alpha = 0.6f) else TextLo.copy(alpha = 0.4f),
                            RoundedCornerShape(8.dp))
                        .clickable { onTouch(); onPick(i); r.onPress(i) }
                        .padding(horizontal = 16.dp, vertical = 8.dp)
                ) { Text(label, color = if (on) Color.White else if (danger) Bad else TextHi, fontSize = 15.sp) }
            }
        }
        is SRow.Header -> Text(r.text, color = Accent, fontSize = 14.sp, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(start = 12.dp, top = 14.dp, bottom = 4.dp))
        is SRow.Info -> Text(r.text, fontSize = 14.sp, modifier = Modifier.padding(horizontal = 12.dp, vertical = 3.dp),
            color = when (r.tone) { Tone.GOOD -> Good; Tone.BAD -> Bad; Tone.MUTED -> TextLo; Tone.NORMAL -> TextHi })
        else -> {
            val enabled = r.focusable
            val bg = if (focused) PanelHi else Color.Transparent
            Row(
                Modifier.fillMaxWidth().background(bg, RoundedCornerShape(10.dp))
                    .border(if (focused) 2.dp else 0.dp, if (focused) Accent else Color.Transparent, RoundedCornerShape(10.dp))
                    .clickable(enabled = enabled) {
                        onTouch()
                        when (r) {
                            is SRow.Action -> r.onA()
                            is SRow.Choice -> r.onPick((r.index + 1) % r.options.size)
                            is SRow.Toggle -> r.onSet(!r.on)
                            else -> {}
                        }
                    }
                    .padding(horizontal = 14.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val label = when (r) {
                    is SRow.Action -> r.label
                    is SRow.Choice -> r.label
                    is SRow.Toggle -> r.label
                    is SRow.Slider -> r.label
                }
                Text(label, color = if (enabled) TextHi else TextLo, fontSize = 17.sp, modifier = Modifier.weight(1f))
                when (r) {
                    is SRow.Action -> Text(r.value, color = TextLo, fontSize = 15.sp, maxLines = 1,
                        overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1.2f))
                    is SRow.Choice -> Text(
                        (if (focused) "◀  " else "") + r.options.getOrElse(r.index) { "" } + (if (focused) "  ▶" else ""),
                        color = if (enabled) TextHi else TextLo, fontSize = 16.sp)
                    is SRow.Toggle -> Text(if (r.on) "On" else "Off", color = if (r.on) Good else TextLo, fontSize = 16.sp)
                    is SRow.Slider -> Row(Modifier.weight(1.2f), verticalAlignment = Alignment.CenterVertically) {
                        Slider(value = r.value.toFloat(), onValueChange = { r.onSet(it.toInt()) },
                            valueRange = r.min.toFloat()..r.max.toFloat(), enabled = enabled, modifier = Modifier.weight(1f))
                        Text("${r.value}${r.suffix}", color = TextHi, fontSize = 15.sp, modifier = Modifier.padding(start = 10.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun ProgressPanel(p: Progress) {
    Box(Modifier.fillMaxSize().background(Color(0xAA000000)), contentAlignment = Alignment.Center) {
        Column(Modifier.width(560.dp).background(Panel, RoundedCornerShape(16.dp)).padding(24.dp)) {
            Text(p.label, color = TextHi, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(14.dp))
            if (p.fraction < 0f) LinearProgressIndicator(Modifier.fillMaxWidth(), color = Accent)
            else LinearProgressIndicator(progress = { p.fraction }, modifier = Modifier.fillMaxWidth(), color = Accent)
            Spacer(Modifier.height(10.dp))
            Text(p.detail, color = TextLo, fontSize = 14.sp)
            Spacer(Modifier.height(14.dp))
            Text("A or B  cancel", color = TextLo, fontSize = 13.sp)
        }
    }
}

@Composable
private fun ConfirmPanel(c: Confirm, nav: MenuNav) {
    Box(Modifier.fillMaxSize().background(Color(0xAA000000)), contentAlignment = Alignment.Center) {
        Column(Modifier.width(560.dp).background(Panel, RoundedCornerShape(16.dp)).padding(24.dp)) {
            Text(c.title, color = TextHi, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            Text(c.body, color = TextLo, fontSize = 15.sp)
            Spacer(Modifier.height(18.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                listOf(c.yes, c.no).forEachIndexed { i, label ->
                    val focused = nav.confirmIndex == i
                    Box(
                        Modifier.padding(start = 10.dp).background(if (focused) Accent else PanelHi, RoundedCornerShape(10.dp))
                            .clickable { nav.confirm = null; if (i == 0) c.onYes() else c.onNo() }
                            .padding(horizontal = 24.dp, vertical = 11.dp)
                    ) { Text(label, color = Color.White, fontSize = 16.sp) }
                }
            }
        }
    }
}

@Composable
fun AppTheme(content: @Composable () -> Unit) = MaterialTheme(colorScheme = AppColors, content = content)
