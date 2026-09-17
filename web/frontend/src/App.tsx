import { Routes, Route } from 'react-router-dom'
import { AuthProvider } from './contexts/AuthContext'
import Nav from './components/Nav'
import ProtectedRoute from './components/ProtectedRoute'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'
import AboutPage from './pages/AboutPage'
import PreOrderPage from './pages/PreOrderPage'
import AccountPage from './pages/AccountPage'
import OrderDetailPage from './pages/OrderDetailPage'
import AuthCallback from './pages/AuthCallback'

export default function App() {
  return (
    <AuthProvider>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
          <Route path="/about" element={<AboutPage />} />
          <Route path="/pre-order" element={<PreOrderPage />} />
          <Route path="/auth/callback" element={<AuthCallback />} />
          <Route path="/account" element={<ProtectedRoute><AccountPage /></ProtectedRoute>} />
          <Route path="/account/orders/:id" element={<ProtectedRoute><OrderDetailPage /></ProtectedRoute>} />
        </Routes>
      </main>
    </AuthProvider>
  )
}
