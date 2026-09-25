# Studio Thread frontend

Product edits now require a current product version and cannot write stock. Use the separate **Adjust stock** form on product/variant edit pages to add or remove units with a reason. The Users screen protects the last admin; the backend also enforces this transactionally. See [admin safety and rollout](../docs/backend-admin-safety.md). Reload the frontend after restarting the matching backend.

A responsive React storefront and separate admin application for the existing Drogon API. Firebase Web SDK handles authentication only. Application records always go through the backend. The existing INR currency and Studio Thread identity are preserved.

## Run

```bash
cd /home/ubuntu/phase1-project/frontend
npm ci
# Only if frontend/.env does not exist:
cp .env.example .env
# Fill in Firebase Web configuration and VITE_API_URL in .env.
npm run dev
```

The existing `.env` was preserved. Do not overwrite it with the example. The development URL is http://localhost:5173. Run the existing backend separately using its existing instructions. Its `ALLOWED_ORIGIN` must match the frontend origin. No backend configuration was changed.

```bash
npm run build
npm run preview
```

For deployment, serve `dist/` and configure your host to fall back to `index.html` for application routes. Set `VITE_API_URL` and Firebase Web variables before building. The preview server uses port 4173; the backend must allow that origin to test live API calls there. Vite environment variables are public browser configuration, not a place for server secrets.

## What is included

- Home with a CSS studio illustration, API-derived category navigation and product selections, benefits and footer.
- Shop with URL-backed search, category/price/availability filters and name/price sorting.
- Product details from the individual product endpoint, stock-aware quantities, related products and missing-image fallbacks.
- Firebase email/password sign-in, registration, session restoration, backend profile verification and role-based routes.
- Server-backed bag, live stock warnings, removal and quantity updates, checkout review, confirmation and expandable order history.
- Account page displaying real profile data.
- Separate admin layout, product CRUD with validation and image preview, order search/filter/sort/detail/status management, user role confirmations, and computed overview metrics.
- Loading, empty, network, authorization, stock and malformed-response states; toast feedback; focus-trapped confirmation dialogs; keyboard focus and reduced-motion support.
- Informational pages clearly identify unpublished contact, social, privacy and terms content.

## Routes and API mapping

| UI route / feature                                                | Backend calls                                                                                                         |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `/`, `/shop`, `/products` (shop redirect)                         | Public `GET /products`; categories, search, filters and selections derived client-side                                |
| `/products/:id` and legacy `/product/:id`                         | Public `GET /products/{id}`; related products use shared catalog                                                      |
| `/login`, `/register`                                             | Firebase Auth, then authenticated `GET /me`                                                                           |
| `/account`                                                        | Shared authenticated profile from `GET /me`                                                                           |
| `/cart`, header badge, add-to-bag buttons                         | `GET /cart`; `POST /cart/add`, `/cart/update`, `/cart/remove`                                                         |
| `/checkout`                                                       | `GET /cart` for review; `POST /orders` with retry key, cart version and reviewed price quote on explicit confirmation |
| `/orders`                                                         | `GET /orders`                                                                                                         |
| `/admin`                                                          | Shared `GET /products`, `GET /admin/orders`, `GET /admin/users`                                                       |
| `/admin/products`                                                 | Shared `GET /products`; `DELETE /products/{id}`                                                                       |
| `/admin/products/new`                                             | `POST /products`                                                                                                      |
| `/admin/products/:id/edit`                                        | `GET /products/{id}`; `PUT /products/{id}`                                                                            |
| `/admin/orders`                                                   | `GET /admin/orders`; `PUT /admin/orders/{id}/status`                                                                  |
| `/admin/users`                                                    | `GET /admin/users`; `PUT /admin/users/{uid}/role`                                                                     |
| `/about`, `/support`, `/contact`, `/privacy`, `/terms`, `/social` | Presentation only                                                                                                     |

Cart, checkout, orders and account require a verified account. Every `/admin/*` route requires the backend-derived admin role. Unknown routes show a not-found page.

## Exact controller contracts

The source of truth is `src/controllers/` and `src/models/` at the repository root.

- Product reads return a plain array or object: `id, name, price, description, imageUrl, stock, category`. Create and update send all six writable fields; `price` is a number and `stock` an integer. Create returns `{ id, success }`; update/delete return `{ success }`.
- `/me` returns `{ uid, email, isAdmin }`, **not** a `role` field. AuthContext maps this verified boolean to `admin` or `customer`.
- `/admin/users` returns `[{ uid, email, role }]`. Role updates send `{ role: "admin" | "customer" }` and return `{ success }`.
- `/cart` returns an enriched array: `{ productId, quantity, name, price, stock, available, imageUrl? }`. Deleted products have zero price/stock and `available: false`. No extra product join is necessary.
- Add sends `{ productId, quantity }` (increments an existing line). Update sends `{ productId, quantity }` (sets quantity; backend removes for zero). Remove sends `{ productId }`. Mutations return `{ success, cart: [{ productId, quantity }] }`; the UI re-fetches the enriched cart afterward.
- Create order sends an `Idempotency-Key` header and `{ cartVersion, shippingAddress }` when available, and returns `{ id, userId, items, total, status, createdAt }`. List endpoints return arrays of that object. Items contain snapshotted `{ productId, name, price, quantity }`. `createdAt` is epoch **seconds**, not milliseconds.
- Status updates send `{ status, expectedStatus }` and follow `pending → paid → shipped → delivered`, with cancellation before shipping. Cancellation restores stock atomically; paid cancellations require manual refund review. See [Stage 2 ordering](../docs/backend-ordering.md).
- Protected calls send Firebase's current `Authorization: Bearer <ID token>`. A 401 triggers one forced token refresh; repeated failure invalidates profile access. A 403 removes cached admin access. Tokens are never manually persisted.
- Controller errors generally use `{ error }`; stock conflicts are HTTP 409 with optional `requested` and `available`. Failed mutations may return only `{ success: false }`. The API layer also handles network failure, timeouts, unreadable JSON and malformed read responses.

## Architecture and changed files

- `src/services/api.js`: centralized HTTP, exact bodies, token handling, response validation and errors.
- `src/services/firebase.js`: existing browser environment setup, now with a recoverable missing-config state.
- `src/context/`: Auth, Cart, Catalog and Toast providers. Catalog data is shared rather than re-fetched by every card/page. Cart refreshes discard obsolete user/session responses and serialize mutations.
- `src/hooks/useResource.js`: asynchronous page loading with stale-response protection.
- `src/components/common/`: shared buttons, loading/empty/error UI, icons, images, quantity controls, modal, boundary and protected routes.
- `src/components/layout/`, `Navbar.jsx`, `ProductCard.jsx`, `orders/OrderDetails.jsx`: reusable layouts and commerce presentation.
- `src/pages/` and `src/pages/admin/`: complete storefront and management routes.
- `src/utils/format.js`: currency/date/link helpers and catalog filtering.
- `src/styles/theme.css`: consolidated, responsive design system replacing the four old page stylesheets.
- `src/App.jsx`, `src/main.jsx`, `index.html`, `public/favicon.svg`: application composition, lazy admin routes and branding.
- `.env.example`, `README.md`, `playwright.config.js`, `tests/`, `.prettierrc.json`, package scripts and lockfile: setup, verification and formatting.
- Root `.gitignore`: ignores browser test output.

The original frontend rebuild preserved backend source. The subsequent [backend correctness update](../docs/backend-correctness.md) changes backend source/CMake and checkout integration; server environment, database rules, credentials and the user brief remain unchanged. No additional runtime library was introduced. Added development dependencies: `@playwright/test` and `prettier`.

## Verification

```bash
npm run test:unit
npx playwright install chromium
npm test
npm run format:check
npm run build
```

Browser tests start an isolated frontend on port 5174 with test-only environment values and intercept Firebase/Auth and backend HTTP calls in the browser. They exercise the actual Firebase Web SDK with simulated Auth responses; no real users, products, orders or roles are modified. There is no test authentication bypass in the app and no fixture import in production code.

Coverage includes guest browsing/filtering/details/login, customer bag mutations/order creation/history, auth restoration and customer admin denial, admin product CRUD/status/roles/self-demotion, registration failures, missing images/products, server failure, empty/malformed catalogs, stock conflicts, expired tokens, and overflow checks at 390/768/1440px. Screenshots and traces are saved under ignored `test-results/`.

### Results in this workspace

- Production build: passed.
- Catalog unit checks: passed.
- Playwright: **15 passed**, including complete customer/admin workflows, role revocation, ambiguous order failure, keyboard modal behavior, and populated responsive screens at 390, 768 and 1440 pixels.
- Formatting and `git diff --check`: passed.
- Homepage and admin screenshots visually reviewed at desktop and mobile sizes.
- Backend/CMake diff: empty. No tracked secret files; no server-private-key or test-fixture markers in the production bundle.
- Live API verification: blocked by the configured backend being unreachable, so the results above are isolated frontend tests rather than live integration certification.

This container required temporary Chromium libraries and browser downloads under `/tmp`. The browser suite was run here with:

```bash
LD_LIBRARY_PATH=/tmp/studio-browser-libs/usr/lib/x86_64-linux-gnu \
PLAYWRIGHT_BROWSERS_PATH=/tmp/studio-playwright npm test -- --workers=3
```

On a normal workstation, use the standard Playwright install/test commands above; Linux machines may also require `npx playwright install-deps chromium`.

## Backend limitations and remaining deployment work

- Live integration must be checked with a running configured backend and real customer/admin sessions. The configured backend was unreachable in this workspace during implementation; browser tests use contract-matched fixtures and cannot certify live Firebase/provider/CORS configuration.
- Email/password registration must be enabled in the existing Firebase project. Initial admin assignment remains a backend/project setup responsibility.
- Checkout now uses a backend transaction and persisted idempotency keys. After an ambiguous failure, the frontend retains the original key/body across reloads. See [backend correctness](../docs/backend-correctness.md) for compatibility, limits and activation instructions.
- Stage 1 makes Firestore read failures explicit; failed collection reads display an error instead of a false empty result.
- The backend follows Firestore pages internally but has no public cursor pagination, payment processing, carrier integration, refund processing, coupons, reviews or sales analytics. Gross order value includes every returned order, including cancelled ones; it is not collected revenue.
- Publish real contact details, legal policies and social destinations before public use. Confirm business currency; INR matches the previous frontend.
- Fonts use Google Fonts with local system fallbacks. Hero artwork is local CSS; product images remain the URLs stored in your API.

### Dependency audit

Compatible `npm audit fix` updates were applied without forcing a major migration. The audit still reports 14 advisories (12 moderate, 2 high) in the existing Firebase/Undici, React Router and Vite/esbuild dependency trees. No claim of a clean security audit is made. Review and migrate the affected dependencies before public deployment; the audit proposes breaking major updates for Vite and React Router. The app uses browser Firebase modules, but the installed dependency tree still includes the flagged Node packages.

## Stage 2 ordering update

Added `/orders/:id` with order details, address snapshot, timeline and cancellation/fulfillment actions; `/admin/inventory` with stock movement history; delivery address entry during checkout; and one-time address capture for legacy orders before shipping. Product deletion now archives inventory records. Checkout retry persistence includes the original address.

Combined verification: 17 browser tests (including new order screens at all three viewport widths), frontend unit tests, production build and formatting checks. Backend and Firestore emulator results and activation instructions are in [Stage 2 ordering](../docs/backend-ordering.md).

## Stage 4 store operations

Added admin Shipping and Audit log pages, order tracking entry, customer Updates, product variant creation/edit/removal and PNG/JPEG/WebP upload. Checkout selects a delivery method, displays shipping in the total and persists its original rate with the retry intent. Variant selection sends a separate sellable product ID, preserving the existing cart/stock workflow. Images under `/media/` resolve against `VITE_API_URL`.

Combined verification: 20 browser tests and frontend unit/build/format checks. Server verification and the new `libpng-dev`/persistent upload storage requirements are documented in [Stage 4 store operations](../docs/backend-store-operations.md). Carrier booking and email/SMS are not connected; order notifications are in-app. Stage 3 payments remains skipped.

Use Node 24 (`nvm use`) for the current dependencies. Install with `npm ci --ignore-scripts`. Checkout now requires an active delivery or pickup method; pending orders reserve stock for four hours. See [launch hardening](../docs/backend-launch-hardening.md) for deployment and CI details.
