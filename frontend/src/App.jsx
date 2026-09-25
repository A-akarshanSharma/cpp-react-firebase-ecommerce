import { lazy, Suspense } from 'react'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import { AuthProvider } from './context/AuthContext'
import { ToastProvider } from './context/ToastContext'
import { CartProvider } from './context/CartContext'
import { CatalogProvider } from './context/CatalogContext'
import { ErrorBoundary, Loader } from './components/common/UI'
import ProtectedRoute from './components/common/ProtectedRoute'
import StoreLayout, { RouteEffects } from './components/layout/StoreLayout'
import HomePage from './pages/HomePage'
import ShopPage from './pages/ShopPage'
import ProductPage from './pages/ProductPage'
import LoginPage from './pages/LoginPage'
import CartPage from './pages/CartPage'
import CheckoutPage from './pages/CheckoutPage'
import NotificationsPage from './pages/NotificationsPage'
import OrdersPage from './pages/OrdersPage'
import OrderDetailPage from './pages/OrderDetailPage'
import AccountPage from './pages/AccountPage'
import InfoPage from './pages/InfoPage'
const AdminLayout = lazy(() => import('./pages/admin/AdminLayout'))
const AdminOverviewPage = lazy(() => import('./pages/admin/AdminOverviewPage'))
const AdminProductsPage = lazy(() => import('./pages/admin/AdminProductsPage'))
const AdminProductFormPage = lazy(() => import('./pages/admin/AdminProductFormPage'))
const AdminOrdersPage = lazy(() => import('./pages/admin/AdminOrdersPage'))
const AdminShippingPage = lazy(() => import('./pages/admin/AdminShippingPage'))
const AdminAuditPage = lazy(() => import('./pages/admin/AdminAuditPage'))
const AdminInventoryPage = lazy(() => import('./pages/admin/AdminInventoryPage'))
const AdminUsersPage = lazy(() => import('./pages/admin/AdminUsersPage'))
export default function App() {
  return (
    <BrowserRouter useTransitions={false}>
      <ErrorBoundary>
        <ToastProvider>
          <AuthProvider>
            <CatalogProvider>
              <CartProvider>
                <a href="#main-content" className="skip-link">
                  Skip to content
                </a>
                {import.meta.env.VITE_PREVIEW_MODE === 'true' && (
                  <div className="announcement" role="note">
                    Demo store · Test orders only. No payments are collected.
                  </div>
                )}
                <RouteEffects />
                <Suspense fallback={<Loader />}>
                  <Routes>
                    <Route element={<StoreLayout />}>
                      <Route index element={<HomePage />} />
                      <Route path="shop" element={<ShopPage />} />
                      <Route path="products" element={<Navigate to="/shop" replace />} />
                      <Route path="products/:id" element={<ProductPage />} />
                      <Route path="product/:id" element={<ProductPage />} />
                      <Route path="login" element={<LoginPage key="login" />} />
                      <Route path="register" element={<LoginPage key="register" register />} />
                      <Route element={<ProtectedRoute />}>
                        <Route path="cart" element={<CartPage />} />
                        <Route path="checkout" element={<CheckoutPage />} />
                        <Route path="orders" element={<OrdersPage />} />
                        <Route path="notifications" element={<NotificationsPage />} />
                        <Route path="orders/:id" element={<OrderDetailPage />} />
                        <Route path="account" element={<AccountPage />} />
                      </Route>
                      {['about', 'support', 'contact', 'privacy', 'terms', 'social'].map((page) => (
                        <Route key={page} path={page} element={<InfoPage page={page} />} />
                      ))}
                      <Route path="*" element={<InfoPage />} />
                    </Route>
                    <Route element={<ProtectedRoute admin />}>
                      <Route path="admin" element={<AdminLayout />}>
                        <Route index element={<AdminOverviewPage />} />
                        <Route path="products" element={<AdminProductsPage />} />
                        <Route path="products/new" element={<AdminProductFormPage key="new" />} />
                        <Route
                          path="products/:id/edit"
                          element={<AdminProductFormPage key="edit" />}
                        />
                        <Route path="orders" element={<AdminOrdersPage />} />
                        <Route path="users" element={<AdminUsersPage />} />
                        <Route path="shipping" element={<AdminShippingPage />} />
                        <Route path="audit" element={<AdminAuditPage />} />
                        <Route path="inventory" element={<AdminInventoryPage />} />
                        <Route path="*" element={<InfoPage />} />
                      </Route>
                    </Route>
                  </Routes>
                </Suspense>
              </CartProvider>
            </CatalogProvider>
          </AuthProvider>
        </ToastProvider>
      </ErrorBoundary>
    </BrowserRouter>
  )
}
