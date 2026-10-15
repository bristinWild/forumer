
import { resolve } from "node:path";

// E3 Step 1 - Forumer headless UI smoke tests.
// Uses the logos-module-builder 0.3.2 integration test runner.

const frameworkRoot = process.env.LOGOS_QT_MCP;

if (!frameworkRoot) {
    throw new Error(
        "LOGOS_QT_MCP is not set. Run through " +
        "`nix build .#integration-test -L`."
    );
}

const { test, run } = await import(
    resolve(frameworkRoot, "test-framework/framework.mjs")
);

function requireMatch(response, description) {
    if (
        response?.error ||
        !Array.isArray(response?.matches) ||
        response.matches.length === 0
    ) {
        throw new Error(
            `${description}: ${response?.error || "not found in QML tree"
            }`
        );
    }
}

// TEST 1: Forumer launches and shows onboarding.

test("Forumer: onboarding screen loads", async (app) => {
    await app.waitFor(
        async () => {
            await app.expectTexts([
                "forumer",
                "create an identity"
            ]);

            requireMatch(
                await app.findByProperty(
                    "objectName",
                    "forumStore"
                ),
                "ForumStore"
            );

            requireMatch(
                await app.findByProperty("mode", "create"),
                "Create mode"
            );
        },
        {
            timeout: 30000,
            interval: 500,
            description: "Forumer onboarding"
        }
    );
});

// TEST 2: Recovery navigation works.

test("Forumer: recovery navigation works", async (app) => {
    await app.click("restore from a phrase instead");

    await app.waitFor(
        async () => {
            requireMatch(
                await app.findByProperty("mode", "restore"),
                "Recovery mode"
            );
        },
        {
            timeout: 10000,
            interval: 300,
            description: "Recovery mode"
        }
    );

    await app.click("create a new identity instead");

    await app.waitFor(
        async () => {
            requireMatch(
                await app.findByProperty("mode", "create"),
                "Return to create mode"
            );
        },
        {
            timeout: 10000,
            interval: 300,
            description: "Create mode"
        }
    );
});

run();
