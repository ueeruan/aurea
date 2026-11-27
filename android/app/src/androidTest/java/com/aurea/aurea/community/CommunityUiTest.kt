package com.aurea.aurea.community

import android.app.Application
import android.graphics.Bitmap
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.home.HomeDock
import com.aurea.aurea.home.HomeViewModel
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Native interaction against isolated fixtures. No real account or public post is used. */
class CommunityUiTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun launch(api: FixtureApi, own: Boolean) {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            val factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application)
            val store: EditorStore = viewModel(factory = factory)
            val conta: ContaViewModel = viewModel(factory = factory)
            AureaTheme { Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
                if (own) Text("aurea", style = AureaType.HeadlineLarge, modifier = Modifier.padding(horizontal = 20.dp))
                Box(Modifier.weight(1f)) { CommunityScreen(store, conta, own, api) }
                HomeDock(if (own) HomeViewModel.PROFILE_TAB else HomeViewModel.COMMUNITY_TAB, {}, {})
            } }
        }
        compose.waitUntil(10000) { api.loaded }
        compose.waitForIdle()
    }
    private fun capture(name: String) {
        compose.waitForIdle()
        Thread.sleep(700) // Wait for the platform keyboard/window exit animation before screenshots.
        val image = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()!!
        File(context.filesDir, "social-$name.png").outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) }; image.recycle()
    }
    @Test fun feedLikeCommentFollowAndBadgeUseTheSameProfiles() {
        val api = FixtureApi(); launch(api, false)
        compose.onNodeWithTag("social.like.${api.postID}").performScrollTo().performClick()
        compose.waitUntil(5000) { api.liked }
        compose.onNodeWithTag("social.like.${api.postID}").assertContentDescriptionEquals("1 " + context.getString(R.string.social_likes))
        compose.onNodeWithTag("social.comments.${api.postID}").performClick()
        compose.onNodeWithTag("social.comment.body").performTextInput("Muito bom!")
        compose.onNodeWithText(context.getString(R.string.social_send)).performClick()
        compose.waitUntil(5000) { api.comment == "Muito bom!" }
        compose.onNodeWithText("Muito bom!").assertIsDisplayed()
        compose.onNodeWithText("@tester").performClick()
        compose.waitUntil(5000) { api.openedProfile == api.ownID }
        compose.onNodeWithTag("social.edit").assertIsDisplayed()
    }
    @Test fun profileEditAndTextPostAreSavedAndRendered() {
        val api = FixtureApi(); launch(api, true)
        compose.onNodeWithTag("social.edit").performClick()
        compose.onNodeWithTag("social.username").performTextReplacement("tester_novo")
        compose.onNodeWithTag("social.name").performTextReplacement("Ruan Tester")
        compose.onNodeWithTag("social.save").performScrollTo().performClick()
        compose.waitUntil(5000) { api.me.getString("username") == "tester_novo" }
        compose.onNodeWithText("@tester_novo").assertIsDisplayed()
        capture("profile")
        compose.onNodeWithTag("social.compose").performScrollTo().performClick()
        compose.onNodeWithTag("social.post.body").performTextInput("Meu primeiro projeto no Aurea")
        compose.onNodeWithTag("social.publish").performScrollTo().performClick()
        compose.waitUntil(5000) { api.newPost != null }
        compose.onNodeWithText("Meu primeiro projeto no Aurea").performScrollTo().assertIsDisplayed()
    }
    @Test fun otherProfileFollowAndAdminVerificationAreRendered() {
        val api = FixtureApi(admin = true); launch(api, false)
        capture("feed")
        compose.onNodeWithText("@motioncreator").performScrollTo().performClick()
        compose.onNodeWithText(context.getString(R.string.social_follow)).performScrollTo().performClick()
        compose.waitUntil(5000) { api.followed }
        compose.onNodeWithText(context.getString(R.string.social_unfollow)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.social_verification)).performScrollTo().performClick()
        compose.onNodeWithText(context.getString(R.string.social_badge_gold)).performClick()
        compose.waitUntil(5000) { api.other.getString("verification") == "gold" }
        compose.onAllNodesWithContentDescription(context.getString(R.string.social_badge_gold)).fetchSemanticsNodes().also { assertTrue(it.isNotEmpty()) }
        capture("verified")
    }
    @Test fun unavailableCommunityOffersRefreshWithoutPretendingAnEmptyFeedIsSuccess() {
        val api = FixtureApi(fail = true); launch(api, false)
        compose.onNodeWithTag("social.error").assertIsDisplayed()
        api.fail = false
        compose.onNodeWithText(context.getString(R.string.social_refresh)).performClick()
        compose.waitUntil(5000) { api.feedRead }
        compose.onNodeWithTag("social.error").assertDoesNotExist()
    }
    @Test fun notificationOpensThePostAndRepliesKeepTheOriginalComment() {
        val api = FixtureApi(activity = true)
        launch(api, false)
        compose.onNodeWithTag("social.activity").assertIsDisplayed().performClick()
        compose.onNodeWithText(context.getString(R.string.social_activity_mark_read)).performClick()
        compose.waitUntil(5000) { api.notificationRead }
        compose.onNodeWithText(context.getString(R.string.social_activity_comment, "Motion Creator")).performClick()
        compose.onNodeWithTag("social.comment.reply.${api.originalCommentID}").performScrollTo().performClick()
        compose.onNodeWithTag("social.comment.body").performTextInput("Minha resposta")
        compose.onNodeWithText(context.getString(R.string.social_send)).performClick()
        compose.waitUntil(5000) { api.reply == api.originalCommentID }
        val replyTag = "social.comment.message.40000000-0000-0000-0000-000000000001"
        compose.waitUntil(5000) { compose.onAllNodesWithTag(replyTag).fetchSemanticsNodes().isNotEmpty() }
        capture("reply-sent")
        compose.onNodeWithTag(replyTag).assertTextEquals("Minha resposta").assertIsDisplayed()
    }
    @Test fun compactFeedKeepsProfileAndPostActionsVisible() {
        val video = File.createTempFile("community-review", ".mp4", context.cacheDir)
        try {
            context.assets.open("review-motion.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
            val api = FixtureApi(activity = true, media = video)
            launch(api, false)
            compose.onNodeWithTag("social.activity").assertIsDisplayed()
            compose.onNodeWithTag("social.myProfile").assertIsDisplayed()
            compose.onNodeWithTag("social.search").assertDoesNotExist()
            val preview = "social.media.preview.${api.mediaID}"
            compose.waitUntil(10000) { compose.onAllNodesWithTag(preview, useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty() }
            val author = compose.onNodeWithTag("social.author.${api.postID}").assertIsDisplayed().getUnclippedBoundsInRoot()
            val artwork = compose.onNodeWithTag("social.post.${api.postID}").assertIsDisplayed().getUnclippedBoundsInRoot()
            val feed = compose.onNodeWithTag("social.feed").getUnclippedBoundsInRoot()
            assertTrue(author.bottom <= artwork.top && artwork.left == feed.left && artwork.right == feed.right)
            val like = compose.onNodeWithTag("social.like.${api.postID}").assertIsDisplayed().getUnclippedBoundsInRoot()
            assertTrue(artwork.bottom <= like.top)
            compose.onNodeWithTag("social.download.${api.postID}").assertIsDisplayed()
            compose.onNodeWithTag("social.comments.${api.postID}").assertIsDisplayed()
            compose.onNodeWithTag("social.share.${api.postID}").assertIsDisplayed()
            capture("feed-reference")
            compose.onNodeWithTag("social.media.open.${api.mediaID}").performClick()
            compose.onNodeWithTag("social.media.player.${api.mediaID}").assertIsDisplayed()
            InstrumentationRegistry.getInstrumentation().uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
            compose.onNodeWithTag("social.feed.options").performClick()
            compose.onNodeWithTag("social.search").assertIsDisplayed()
            compose.onNodeWithTag("social.myProfile").performClick()
            compose.waitUntil(5000) { api.openedProfile == api.ownID }
            compose.onNodeWithTag("social.edit").assertIsDisplayed()
        } finally { video.delete() }
    }

    private class FixtureApi(val admin: Boolean = false, @Volatile var fail: Boolean = false, val activity: Boolean = false, val media: File? = null) : CommunityApi("") {
        val ownID = "10000000-0000-0000-0000-000000000001"
        val otherID = "20000000-0000-0000-0000-000000000001"
        val postID = "30000000-0000-0000-0000-000000000001"
        val mediaID = "60000000-0000-0000-0000-000000000001"
        val originalCommentID = "40000000-0000-0000-0000-000000000002"
        @Volatile var loaded = false; @Volatile var liked = false; @Volatile var followed = false
        @Volatile var feedRead = false; @Volatile var openedProfile = ""
        @Volatile var comment = ""; @Volatile var newPost: String? = null
        @Volatile var notificationRead = false; @Volatile var reply: String? = null
        val me = p(ownID, "tester", "Aurea Tester", "green")
        val other = p(otherID, "motioncreator", "Motion Creator", "blue")
        fun p(id: String, user: String, name: String, badge: String) = JSONObject().put("id", id).put("username", user).put("name", name)
            .put("bio", "Motion, text and ideas in Aurea.").put("verification", badge).put("followers", 12).put("following", 5).put("posts", 1)
        fun post(body: String, author: JSONObject, id: String = postID) = JSONObject().put("id", id).put("body", body).put("createdAt", System.currentTimeMillis() - 3600000)
            .put("author", author).put("likes", if (liked) 1 else 0).put("comments", if (comment.isEmpty()) 0 else 1).put("liked", liked)
        override fun mediaUrl(asset: CommunityAsset): String? = media?.let { Uri.fromFile(it).toString() }
        @Synchronized override fun request(path: String, method: String, body: JSONObject?): JSONObject {
            loaded = true
            if (fail) throw CommunityFailure("community_unavailable")
            if (path.substringBefore('?') == "/me") {
                if (method == "PUT") for (key in listOf("username", "name", "bio")) me.put(key, body!!.getString(key))
                return JSONObject().put("profile", me).put("canVerify", admin).put("activity", activity).put("mediaPosts", activity)
            }
            if (path == "/store/config") return JSONObject().put("uploads", false)
            if (path == "/notifications/read") { notificationRead = true; return JSONObject().put("ok", true) }
            if (path.startsWith("/notifications")) {
                val stamp = System.currentTimeMillis()
                return JSONObject().put("unread", if (notificationRead) 0 else 1).put("through", stamp)
                    .put("items", JSONArray().put(JSONObject().put("id", originalCommentID).put("post", postID)
                        .put("kind", "comment").put("name", "Motion Creator").put("createdAt", stamp).put("unread", !notificationRead)))
            }
            if (path == "/posts/$postID") return post("Publicação da notificação", other)
            if (path.startsWith("/profiles/")) {
                val own = path.contains(ownID); val profile = if (own) me else other
                if (path.endsWith("/follow")) { followed = method == "PUT"; profile.put("followed", followed) }
                else if (path.endsWith("/verification")) profile.put("verification", body!!.getString("verification"))
                else openedProfile = profile.getString("id")
                return JSONObject().put("profile", profile)
            }
            if (path == "/posts/$postID/like") { liked = method == "PUT"; return JSONObject().put("likes", if (liked) 1 else 0).put("liked", liked) }
            if (path == "/posts/$postID/comments") {
                if (method == "POST") {
                    comment = body!!.getString("body"); reply = body.optString("replyTo")
                    return JSONObject().put("id", "40000000-0000-0000-0000-000000000001")
                }
                return JSONObject().put("items", JSONArray().apply {
                    if (activity) put(post("Comentário inicial", other, originalCommentID))
                    if (comment.isNotEmpty()) put(post(comment, me, "40000000-0000-0000-0000-000000000001")
                        .put("replyName", if (activity) "Motion Creator" else JSONObject.NULL))
                })
            }
            if (path == "/posts" && method == "POST") { newPost = body!!.getString("body"); return JSONObject().put("id", "50000000-0000-0000-0000-000000000001") }
            if (path.startsWith("/posts?")) { feedRead = true; return JSONObject().put("items", JSONArray().apply {
                newPost?.let { put(post(it, me, "50000000-0000-0000-0000-000000000001")) }
                if (!path.contains(ownID)) {
                    val item = post("Meu novo preset de movimento. Feito no Aurea.", other)
                    if (media != null) item.put("media", JSONObject().put("id", mediaID)
                        .put("kind", "video").put("name", "Demo.mp4").put("bytes", media.length()))
                    put(item)
                    if (media != null) put(post("Compartilhe seu projeto com a comunidade.", me, "30000000-0000-0000-0000-000000000002"))
                }
            }) }
            throw AssertionError("Unhandled fixture $method $path")
        }
    }
}
