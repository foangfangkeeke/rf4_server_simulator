package gameserver

import (
	_ "embed"
	"encoding/json"
	"strconv"
	"strings"
)

const remoteAssistantUID int64 = 2147483645
const remoteAssistantName = "远程访问助手"

type remoteFacility struct {
	Code     int    `json:"code"`
	Name     string `json:"name"`
	ID       string `json:"id"`
	Panel    string `json:"panel"`
	Category string `json:"category"`
}

//go:embed assets/remote_facilities.json
var remoteFacilityJSON []byte

var remoteFacilitiesByLevel = func() map[string][]remoteFacility {
	var rows map[string][]remoteFacility
	if err := json.Unmarshal(remoteFacilityJSON, &rows); err != nil {
		panic(err)
	}
	return rows
}()

func remoteFacilityCategory(text string) string {
	switch text {
	case "商店", "钓具", "钓具商店":
		return "商店"
	case "食品", "食品商店":
		return "食品"
	case "咖啡":
		return "咖啡"
	case "修理", "工坊":
		return "修理"
	case "管理", "管理处":
		return "管理"
	case "银行", "ATM", "atm":
		return "取款"
	case "工具", "工具店", "工具商店":
		return "工具"
	case "特色", "特色店", "特色商店", "农贸店", "农贸商店":
		return "特色"
	default:
		return text
	}
}

func remoteFacilitiesForText(level, text string) []remoteFacility {
	text = strings.TrimSpace(text)
	rows := remoteFacilitiesByLevel[level]
	// Exact names and map-local numbers take precedence over category aliases.
	var found []remoteFacility
	for _, row := range rows {
		if text == strconv.Itoa(row.Code) || strings.ReplaceAll(text, "\u00a0", " ") == strings.ReplaceAll(row.Name, "\u00a0", " ") {
			found = append(found, row)
		}
	}
	if len(found) != 0 {
		return found
	}
	category := remoteFacilityCategory(text)
	for _, row := range rows {
		if row.Category == category {
			found = append(found, row)
		}
	}
	return found
}

func remoteFacilityMenu(rows []remoteFacility, title string) string {
	lines := []string{title}
	for _, row := range rows {
		lines = append(lines, strconv.Itoa(row.Code)+" "+row.Name)
	}
	return strings.Join(append(lines, "发送名称或编号。"), "\n")
}

func remoteAssistantMenu(level string) string {
	rows := remoteFacilitiesByLevel[level]
	if len(rows) == 0 {
		return "当前地图暂未配置远程设施。"
	}
	return remoteFacilityMenu(rows, "当前地图可打开：")
}

func remoteAssistantReply(level, text string) string {
	rows := remoteFacilitiesForText(level, text)
	if len(rows) == 1 {
		return "正在打开当前地图" + rows[0].Name + "。"
	}
	if len(rows) > 1 {
		return remoteFacilityMenu(rows, "找到多个设施，请发送编号：")
	}
	return remoteAssistantMenu(level)
}

func (s *Server) receiveRemoteAssistant(ctx *Ctx, text string) ([]byte, bool) {
	if !ctx.ExpectResponse {
		return nil, true
	}
	uid := ctx.Sess.Userid
	if uid <= 0 {
		ctx.ReplyFailure("System.InvalidOperationException", "请先登录")
		return nil, true
	}
	c := ctx.Sess.pushCtx()
	tx, err := beginEconomyTx(c, s.DB)
	if err != nil {
		ctx.ReplyFailure("System.IO.IOException", "助手回复保存失败")
		return nil, true
	}
	defer func() { _ = tx.Rollback() }()
	mailID, err := insertContactReply(c, tx, uid, remoteAssistantUID, remoteAssistantReply(s.currentLevel(uid), text))
	if err == nil {
		err = tx.Commit()
	}
	if err != nil {
		ctx.ReplyFailure("System.IO.IOException", "助手回复保存失败")
		return nil, true
	}
	ctx.AfterResponse(func() { s.notifyMail(ctx.Sess.pushCtx(), uid, mailID) })
	return []byte{0}, true
}
