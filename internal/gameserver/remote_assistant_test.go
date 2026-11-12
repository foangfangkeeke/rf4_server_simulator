package gameserver

import (
	"strconv"
	"strings"
	"testing"

	"rf4server/internal/proto"
)

func sendRemoteAssistantForTest(t *testing.T, s *Server, sess *Session, text string) {
	t.Helper()
	ctx := miscCtx(sess, 19, 1, rigRequestTree(t, proto.U32(uint32(remoteAssistantUID)), proto.StrVal(text)))
	ctx.ExpectResponse = true
	res := s.dispatch.executeRequest(ctx, true)
	if res.nativeFailure || len(res.body) != 1 || res.body[0] != 0 {
		t.Fatalf("assistant request failed: %q", res.body)
	}
}

func TestRemoteAssistantCommandsAndContact(t *testing.T) {
	env := newDLineTestEnv(t, 1000001)
	if _, err := env.db.EnsureProfile(t.Context(), env.sess.Userid); err != nil {
		t.Fatal(err)
	}
	env.srv.setLevel(env.sess.Userid, "level_001_lake")
	card := decodeWrappedBytes(t, env.invoke(t, 24, 19, proto.Args524(proto.U32(uint32(remoteAssistantUID)))))
	if card.TypeID != 2002 || card.Fields[1].StrOr() != remoteAssistantName || card.Fields[3].U32() != 0 {
		t.Fatalf("invalid assistant card: %+v", card)
	}
	for _, tc := range []struct{ text, reply string }{
		{"商店", "正在打开当前地图钓鱼站。"},
		{"1", "正在打开当前地图钓鱼站。"},
		{"鱼市", "正在打开当前地图鱼市。"},
		{"4", "正在打开当前地图鱼市。"},
		{"咖啡厅", "正在打开当前地图咖啡厅。"},
		{"工坊", "正在打开当前地图工作坊。"},
		{"6", "正在打开当前地图工作坊。"},
		{"取款机", "正在打开当前地图取款机。"},
		{"食品店", "正在打开当前地图食品店。"},
		{"菜单", remoteAssistantMenu("level_001_lake")},
		{"未知", remoteAssistantMenu("level_001_lake")},
	} {
		sendRemoteAssistantForTest(t, env.srv, env.sess, tc.text)
		mails, err := env.db.MailList(t.Context(), env.sess.Userid, false)
		if err != nil || len(mails) == 0 || mails[0].Sender != remoteAssistantUID || !strings.Contains(mails[0].Text, tc.reply) {
			t.Fatalf("%s: reply=%+v err=%v", tc.text, mails, err)
		}
	}
	var profiles int
	if err := env.db.SQL.QueryRow(`SELECT COUNT(*) FROM profile WHERE uid=?`, remoteAssistantUID).Scan(&profiles); err != nil || profiles != 0 {
		t.Fatalf("virtual assistant became a player: %d %v", profiles, err)
	}
}

func TestRemoteAssistantMenuIsMapSpecific(t *testing.T) {
	bear := remoteAssistantMenu("level_007_grass_lake")
	for _, want := range []string{"1 食品店", "2 鱼市", "3 管理机构"} {
		if !strings.Contains(bear, want) {
			t.Fatalf("菜单缺少 %q: %s", want, bear)
		}
	}
	for _, absent := range []string{"钓鱼站", "工作坊", "取款机", "咖啡厅"} {
		if strings.Contains(bear, absent) {
			t.Fatalf("不存在的设施 %q: %s", absent, bear)
		}
	}
	if got := remoteAssistantMenu("level_unknown"); got != "当前地图暂未配置远程设施。" {
		t.Fatal(got)
	}
	if got := remoteAssistantReply("level_007_grass_lake", "工坊"); got != bear {
		t.Fatal(got)
	}
}

func TestRemoteAssistantSelectsFacilityInstance(t *testing.T) {
	level := "level_002_kirzah_river"
	for _, tc := range []struct{ command, id string }{
		{"小锯木厂", "sawmill_02"}, {"9", "sawmill_02"},
		{"工业品商店", "tool_store_002"}, {"10", "tool_store_002"},
		{"Monkey-Cola 自动贩卖机", "monkeycola_002"},
		{"食品店", "food_store_02"},
	} {
		rows := remoteFacilitiesForText(level, tc.command)
		if len(rows) != 1 || rows[0].ID != tc.id {
			t.Fatalf("%s: %+v", tc.command, rows)
		}
	}
	if got := remoteAssistantReply(level, "工具"); !strings.Contains(got, "找到多个设施") || !strings.Contains(got, "9 小锯木厂") || !strings.Contains(got, "10 工业品商店") || strings.Contains(got, "正在打开") {
		t.Fatal(got)
	}
	// Two shops with the same display name must remain distinct choices.
	rows := remoteFacilitiesForText("level_005_volhov", "钓鱼站")
	if len(rows) != 2 || rows[0].ID == rows[1].ID {
		t.Fatalf("%+v", rows)
	}
	if got := remoteAssistantReply("level_005_volhov", "钓鱼站"); !strings.Contains(got, "请发送编号") {
		t.Fatal(got)
	}
	for _, row := range rows {
		chosen := remoteFacilitiesForText("level_005_volhov", strconv.Itoa(row.Code))
		if len(chosen) != 1 || chosen[0].ID != row.ID {
			t.Fatalf("%+v", chosen)
		}
	}
	if rows := remoteFacilitiesForText("level_007_grass_lake", "小锯木厂"); len(rows) != 0 {
		t.Fatal(rows)
	}
}

func TestRemoteAssistantCatalog(t *testing.T) {
	if len(remoteFacilitiesByLevel) != 18 {
		t.Fatal("地图数量错误")
	}
	validPanel := map[string]bool{"shop": true, "shop-workshop-store": true, "shop-workshop-repair": true, "workshop": true, "cafe": true, "fish-market": true}
	for level, rows := range remoteFacilitiesByLevel {
		seen := map[string]bool{}
		for i, row := range rows {
			key := row.Panel + "/" + row.ID
			if row.Code != i+1 || row.Name == "" || row.ID == "" || row.Category == "" || !validPanel[row.Panel] || seen[key] {
				t.Fatalf("%s: %+v", level, row)
			}
			seen[key] = true
			if strings.Contains(row.ID, "boatstation") || strings.Contains(row.ID, "rentbike") {
				t.Fatalf("不支持远程的载具设施: %+v", row)
			}
		}
	}
}

func TestRemoteAssistantDoesNotTakeItems(t *testing.T) {
	env := newDLineTestEnv(t, 1000001)
	seedEconomyItemDef(t, env.db, 9453, "remote_assistant_mail_item", 27)
	item := economyTestItem(t, env.srv, env.sess.Userid, "remote_assistant_mail_item", "backpack", 3)
	env.invoke(t, 19, 2, proto.Args524(proto.U32(uint32(remoteAssistantUID)), proto.U32(1), proto.Guid(item.GUID)))
	if got := mustItem(t, env.db, item.GUID); got.UID != env.sess.Userid || got.Qty != 3 {
		t.Fatal("assistant received player items")
	}
}
